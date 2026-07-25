#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <string>
uint32_t g_millis = 0;
int g_pinState[64];
struct FakeSerial; 
#include "Arduino.h"
#include "Wire.h"
#include "usb_mpa.h"
FakeSerial Serial1;
TwoWire Wire;
extern std::vector<std::vector<uint8_t>> g_sent;
#include "teensympa.ino"

// ---- harness ----
static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("  FAIL: %s\n", msg); failures++; } else printf("  ok  : %s\n", msg); } while(0)

static void advance(uint32_t ms){ for(uint32_t i=0;i<ms;i++){ g_millis++; loop(); } }
static const std::vector<uint8_t>& lastPkt(){ return g_sent.back(); }
static bool btn(int b){ const auto&p=lastPkt(); return b<8 ? (p[0]>>b)&1 : (p[1]>>(b-8))&1; }
static uint8_t hat(){ return lastPkt()[2]; }

int main(){
  setup();
  loop();
  printf("\n== 1. Alesis note map ==\n");
  CHECK(noteZone[58]==ZONE_GREEN,       "note 58 (Alesis tom3 rim) -> GREEN pad, not blue cymbal");
  CHECK(noteZone[39]==ZONE_GREEN,       "note 39 (Alesis tom4 rim) -> GREEN pad (was unmapped)");
  CHECK(noteZone[23]==ZONE_YELLOW_CYM,  "note 23 (half-open hat) -> yellow cymbal (was unmapped)");
  CHECK(noteZone[21]==ZONE_YELLOW_CYM,  "note 21 (splash) -> yellow cymbal (was unmapped)");
  CHECK(noteZone[44]==ZONE_NONE,        "note 44 (hi-hat pedal chick) ignored by default");
  CHECK(noteZone[36]==ZONE_KICK && noteZone[38]==ZONE_RED, "kick 36 / snare 38");

  printf("\n== 2. hi-hat pedal CC must not press Start ==\n");
  onControlChange(10, 4, 127);   // CC#4 pedal to the floor
  advance(2);
  CHECK(!btn(MPA_BTN_START), "CC#4 at 127 does NOT press Start");

  printf("\n== 3. a snare hit ==\n");
  size_t before=g_sent.size();
  onNoteOn(10,38,100); advance(2);
  CHECK(g_sent.size()>before,        "a report was sent");
  CHECK(btn(MPA_BTN_CIRCLE),         "red pad -> Circle");
  CHECK(btn(MPA_BTN_PAD_FLAG),       "pad flag set");
  CHECK(!btn(MPA_BTN_CYMBAL_FLAG),   "cymbal flag clear");
  CHECK(hat()==MPA_HAT_NEUTRAL,      "hat centred");
  advance(NOTE_ON_TIME+4);
  CHECK(!btn(MPA_BTN_CIRCLE),        "released after NOTE_ON_TIME");

  printf("\n== 4. cymbal hat encoding ==\n");
  onNoteOn(10,46,100); advance(2);
  CHECK(btn(MPA_BTN_TRIANGLE) && btn(MPA_BTN_CYMBAL_FLAG) && hat()==MPA_HAT_UP, "hi-hat -> Triangle + cymbal flag + hat UP");
  advance(NOTE_ON_TIME+4);
  onNoteOn(10,51,100); advance(2);
  CHECK(btn(MPA_BTN_SQUARE) && hat()==MPA_HAT_DOWN, "ride -> Square + hat DOWN");
  advance(NOTE_ON_TIME+4);
  onNoteOn(10,49,100); advance(2);
  CHECK(btn(MPA_BTN_CROSS) && btn(MPA_BTN_CYMBAL_FLAG) && hat()==MPA_HAT_NEUTRAL, "crash -> Cross + cymbal flag + hat NEUTRAL");
  advance(NOTE_ON_TIME+4);

  printf("\n== 5. D-pad ==\n");
  Wire.rawSwitches0 &= ~0x02;      // UP pressed (P0_1, active low)
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(hat()==MPA_HAT_UP, "Qw/ST Pad UP -> hat UP (proves bit1, not bit0)");
  CHECK(!btn(MPA_BTN_CYMBAL_FLAG), "D-pad sets no cymbal flag");
  Wire.rawSwitches0 &= ~0x10;      // + DOWN too
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  Wire.rawSwitches0 |= 0x02;       // release UP, keep DOWN
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(hat()==MPA_HAT_DOWN, "Qw/ST Pad DOWN -> hat DOWN (bit4)");
  Wire.rawSwitches0 |= 0x10; Wire.rawSwitches0 &= ~0x04; // LEFT
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(hat()==MPA_HAT_LEFT, "LEFT -> hat LEFT (bit2)");
  Wire.rawSwitches0 &= ~0x02;  // UP+LEFT
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(hat()==MPA_HAT_UP_LEFT, "UP+LEFT -> diagonal");
  Wire.rawSwitches0 = 0xFF;
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));

  printf("\n== 6. cymbal wins the hat over a held D-pad ==\n");
  Wire.rawSwitches0 &= ~0x02;   // hold UP
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  onNoteOn(10,51,100); advance(2);     // ride while holding UP
  CHECK(hat()==MPA_HAT_DOWN, "ride cymbal overrides a held D-pad UP (this is the bug the other way round)");
  advance(NOTE_ON_TIME+4);
  CHECK(hat()==MPA_HAT_UP, "D-pad regains the hat once the cymbal releases");
  Wire.rawSwitches0 = 0xFF;
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));

  printf("\n== 7. face buttons and Start/Select ==\n");
  Wire.rawSwitches1 &= ~0x40;   // A (P1_6)
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(btn(MPA_BTN_CROSS) && !btn(MPA_BTN_PAD_FLAG) && !btn(MPA_BTN_CYMBAL_FLAG),
        "pad A -> Cross with NO pad/cymbal flag (reads as a gamepad press)");
  Wire.rawSwitches1 = 0xFF;
  Wire.rawSwitches0 &= ~0x20;   // minus  = Select
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(btn(MPA_BTN_SELECT), "'-' -> Select");
  Wire.rawSwitches1 &= ~0x08;   // plus  (P1_3) too -> hold for PS
  advance(PS_BUTTON_HOLD_TIME+20);
  CHECK(btn(MPA_BTN_PS) && !btn(MPA_BTN_START) && !btn(MPA_BTN_SELECT),
        "'+'+'-' held -> PS button, Start/Select suppressed");
  Wire.rawSwitches0=0xFF; Wire.rawSwitches1=0xFF;
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));

  printf("\n== 7b. every Qw/ST Pad bit maps to the right thing ==\n");
  struct { uint8_t port; uint8_t bit; const char* name; uint16_t mask; } bits[] = {
    {0,1,"UP",QP_UP},{0,2,"LEFT",QP_LEFT},{0,3,"RIGHT",QP_RIGHT},{0,4,"DOWN",QP_DOWN},
    {0,5,"MINUS",QP_MINUS},{1,3,"PLUS",QP_PLUS},{1,4,"B",QP_B},{1,5,"Y",QP_Y},
    {1,6,"A",QP_A},{1,7,"X",QP_X} };
  for (auto &e : bits) {
    Wire.rawSwitches0=0xFF; Wire.rawSwitches1=0xFF;
    advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
    if(e.port==0) Wire.rawSwitches0 &= ~(1<<e.bit); else Wire.rawSwitches1 &= ~(1<<e.bit);
    advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
    char msg[80]; snprintf(msg,sizeof msg,"P%u_%u -> %s (mask 0x%04X)",e.port,e.bit,e.name,e.mask);
    CHECK(padRaw==e.mask, msg);
  }
  Wire.rawSwitches0=0xFF; Wire.rawSwitches1=0xFF;
  advance(DPAD_POLL_INTERVAL*(DPAD_DEBOUNCE_SAMPLES+2));
  CHECK(padRaw==0, "nothing pressed reads as exactly zero (LED and NC pins masked off)");

  printf("\n== 8. retrigger gap (fast roll) ==\n");
  g_sent.clear();
  onNoteOn(10,38,100); advance(6);
  bool wasOn = btn(MPA_BTN_CIRCLE);
  onNoteOn(10,38,100);                   // second hit 6 ms later, still held
  bool sawRelease=false, backOn=false;
  for(int i=0;i<NOTE_ON_TIME+10;i++){ g_millis++; loop();
    if(!btn(MPA_BTN_CIRCLE)) sawRelease=true;
    else if(sawRelease) backOn=true; }
  CHECK(wasOn, "first hit registered");
  CHECK(sawRelease && backOn, "second hit forced an off/on edge so the console counts two notes");

  printf("\n== 9. no unbounded hold from a sustained roll ==\n");
  g_millis+=100; loop();
  for(int i=0;i<40;i++){ onNoteOn(10,38,100); advance(5); }   // 40 hits, 5 ms apart
  uint32_t t0=g_millis;
  while(btn(MPA_BTN_CIRCLE) && g_millis-t0 < 500){ g_millis++; loop(); }
  CHECK(g_millis-t0 <= NOTE_ON_TIME+RETRIGGER_GAP_TIME+2,
        "released within one NOTE_ON_TIME of the last hit (old '+=' code would hold ~1s)");

  printf("\n== 10. millis() rollover ==\n");
  g_millis = 0xFFFFFF00; loop();
  onNoteOn(10,38,100); advance(2);
  CHECK(btn(MPA_BTN_CIRCLE), "hit registers just before the 49.7-day wrap");
  advance(0x200);   // walk through the wrap
  CHECK(!btn(MPA_BTN_CIRCLE), "and releases correctly across it");

  printf("\n%s (%d failure(s))\n", failures? "FAILED":"ALL PASSED", failures);
  return failures?1:0;
}
