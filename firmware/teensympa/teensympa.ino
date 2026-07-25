// ---------------------------------------------------------------------------
// teensympa - a DIY MIDI Pro Adapter for Rock Band
//
// Listens to a USB-MIDI electronic drum module on the Teensy's USB host port
// and presents itself to the console as a Mad Catz / Harmonix Rock Band drum
// controller, with an optional D-pad for menu navigation.
//
// Target: Teensy 4.1 (also builds for 4.0 and, unchanged, for 3.6)
// Requires: USB Type set to "MPA (Rock Band drum adapter)" - see
//           tools/install_mpa_core.py
//
// Based on curiousjp's original Teensy 3.6 firmware.
// ---------------------------------------------------------------------------

#include <USBHost_t36.h>
#include "config.h"
#include "notemap.h"

#if DPAD_MODE == DPAD_QWSTPAD
  #include <Wire.h>
  #include "qwstpad.h"
#endif

// ---------------------------------------------------------------------------
// Build sanity checks. These fail loudly at compile time rather than producing
// a board that enumerates as something useless.
// ---------------------------------------------------------------------------
#if !defined(MPA_INTERFACE)
  #error "Set Tools > USB Type to 'MPA (Rock Band drum adapter)'. If that entry is missing, run tools/install_mpa_core.py first."
#endif

// WProgram.h already pulls this in, but being explicit means the dependency
// survives someone reordering the core's includes.
#include "usb_mpa.h"

#if defined(__IMXRT1062__)
  #define BOARD_NAME "Teensy 4.x"
#elif defined(__MK66FX1M0__)
  #define BOARD_NAME "Teensy 3.6"
#else
  #warning "Untested board - this firmware targets Teensy 4.1 / 4.0 / 3.6"
  #define BOARD_NAME "unknown"
#endif

// ---------------------------------------------------------------------------
// Debug plumbing
// ---------------------------------------------------------------------------
#ifdef DEBUG_ENABLED
  #define DBG_BEGIN()    do { DEBUG_PORT.begin(DEBUG_BAUD); } while (0)
  #define DBG(...)       do { DEBUG_PORT.print(__VA_ARGS__); } while (0)
  #define DBGLN(...)     do { DEBUG_PORT.println(__VA_ARGS__); } while (0)
  #define DBGF(...)      do { DEBUG_PORT.printf(__VA_ARGS__); } while (0)
#else
  #define DBG_BEGIN()    do {} while (0)
  #define DBG(...)       do {} while (0)
  #define DBGLN(...)     do {} while (0)
  #define DBGF(...)      do {} while (0)
#endif

// ---------------------------------------------------------------------------
// USB host
// ---------------------------------------------------------------------------
USBHost     usbHost;
USBHub      hubOne(usbHost);
USBHub      hubTwo(usbHost);
MIDIDevice  midiInput(usbHost);

// ---------------------------------------------------------------------------
// Zone state
//
// The original stored a countdown per pad and subtracted the loop's elapsed
// time from it. Two problems with that: every hit did `+= NOTE_ON_TIME`, so a
// fast roll pushed the countdown up without bound and the pad could stay stuck
// on for far longer than intended; and the code carried a comment warning you
// to reboot the adapter every 50 days because of millis() rollover.
//
// Absolute deadlines compared with signed differences fix both. A hit re-arms
// the deadline instead of extending it, and (int32_t)(now - deadline) is
// correct across the 49.7 day wrap with no special handling.
// ---------------------------------------------------------------------------
struct ZoneState {
  uint32_t onUntil;      // active while now < onUntil
  uint32_t gapUntil;     // forced-off window, active only once now >= gapUntil
  uint32_t lastTrigger;  // for double-trigger rejection
  uint8_t  velocity;
};

static ZoneState zones[ZONE_COUNT];
uint8_t noteZone[128];   // definition for the extern in notemap.h

static inline bool reached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

static inline bool zoneActive(uint8_t z, uint32_t now) {
  const ZoneState &s = zones[z];
  return !reached(now, s.onUntil) && reached(now, s.gapUntil);
}

static void triggerZone(uint8_t z, uint8_t velocity, uint32_t now) {
  ZoneState &s = zones[z];

#if TRIGGER_DEBOUNCE_TIME > 0
  if (s.lastTrigger && (uint32_t)(now - s.lastTrigger) < TRIGGER_DEBOUNCE_TIME) {
    DBGF("  debounced zone %u\n", z);
    return;
  }
#endif
  s.lastTrigger = now ? now : 1;
  s.velocity = velocity;

#if RETRIGGER_GAP_TIME > 0
  if (zoneActive(z, now)) {
    // Already held. Drop the button for a moment so the console sees a fresh
    // press rather than one continuous hold - otherwise rolls, flams and buzz
    // strokes register as a single note.
    s.gapUntil = now + RETRIGGER_GAP_TIME;
    s.onUntil  = s.gapUntil + NOTE_ON_TIME;
    return;
  }
#endif
  s.gapUntil = now;
  s.onUntil  = now + NOTE_ON_TIME;
}

// ---------------------------------------------------------------------------
// Start / Select sources
// ---------------------------------------------------------------------------
static bool ccButtonPressed = false;

// ---------------------------------------------------------------------------
// D-pad
// ---------------------------------------------------------------------------
#if DPAD_MODE != DPAD_NONE
struct PadButtons {
  bool up, down, left, right;
  bool a, b, x, y;
  bool start, select;
};
static PadButtons pad;             // debounced, current
static uint32_t   lastPadPoll = 0;
static uint32_t   bothHeldSince = 0;
static bool       psButtonLatched = false;
#endif

#if DPAD_MODE == DPAD_QWSTPAD
static QwSTPad  qwstpad(QWSTPAD_I2C_BUS, QWSTPAD_I2C_ADDR);
static uint16_t padRaw = 0;
static uint16_t padCandidate = 0;
static uint8_t  padStable = 0;
static uint32_t lastPadRetry = 0;
#endif

#if DPAD_MODE != DPAD_NONE
static void pollDpad(uint32_t now) {
  if ((uint32_t)(now - lastPadPoll) < DPAD_POLL_INTERVAL) return;
  lastPadPoll = now;

#if DPAD_MODE == DPAD_QWSTPAD
  if (!qwstpad.present()) {
    // Retry once a second so hot-plugging the pad works.
    if ((uint32_t)(now - lastPadRetry) > 1000) {
      lastPadRetry = now;
      if (qwstpad.reconnect()) DBGLN("Qw/ST Pad reconnected");
    }
    return;
  }

  uint16_t sample = qwstpad.read();
  // The TCA9555 has no debounce and neither do the tactile switches, so a
  // reading only counts once it has repeated.
  if (sample == padCandidate) {
    if (padStable < DPAD_DEBOUNCE_SAMPLES) padStable++;
  } else {
    padCandidate = sample;
    padStable = 1;
  }
  if (padStable >= DPAD_DEBOUNCE_SAMPLES) padRaw = padCandidate;

  pad.up     = padRaw & QP_UP;
  pad.down   = padRaw & QP_DOWN;
  pad.left   = padRaw & QP_LEFT;
  pad.right  = padRaw & QP_RIGHT;
  pad.a      = padRaw & QP_A;
  pad.b      = padRaw & QP_B;
  pad.x      = padRaw & QP_X;
  pad.y      = padRaw & QP_Y;
  pad.start  = padRaw & QP_PLUS;
  pad.select = padRaw & QP_MINUS;

#elif DPAD_MODE == DPAD_GPIO
  pad.up     = (digitalRead(DPAD_UP_PIN)     == LOW);
  pad.down   = (digitalRead(DPAD_DOWN_PIN)   == LOW);
  pad.left   = (digitalRead(DPAD_LEFT_PIN)   == LOW);
  pad.right  = (digitalRead(DPAD_RIGHT_PIN)  == LOW);
  pad.a      = (digitalRead(DPAD_A_PIN)      == LOW);
  pad.b      = (digitalRead(DPAD_B_PIN)      == LOW);
  pad.x      = false;
  pad.y      = false;
  pad.start  = (digitalRead(DPAD_START_PIN)  == LOW);
  pad.select = (digitalRead(DPAD_SELECT_PIN) == LOW);
#endif

  // Start + Select held together = PS / Home button.
  if (pad.start && pad.select) {
    if (!bothHeldSince) bothHeldSince = now ? now : 1;
    if (!psButtonLatched && (uint32_t)(now - bothHeldSince) >= PS_BUTTON_HOLD_TIME) {
      psButtonLatched = true;
      DBGLN("PS button");
    }
  } else {
    bothHeldSince = 0;
    psButtonLatched = false;
  }
}

// Encode the eight compass directions, favouring whichever pair is held.
static uint8_t dpadHat() {
  if (pad.up    && pad.right) return MPA_HAT_UP_RIGHT;
  if (pad.up    && pad.left)  return MPA_HAT_UP_LEFT;
  if (pad.down  && pad.right) return MPA_HAT_DOWN_RIGHT;
  if (pad.down  && pad.left)  return MPA_HAT_DOWN_LEFT;
  if (pad.up)                 return MPA_HAT_UP;
  if (pad.down)               return MPA_HAT_DOWN;
  if (pad.left)               return MPA_HAT_LEFT;
  if (pad.right)              return MPA_HAT_RIGHT;
  return MPA_HAT_NEUTRAL;
}
#endif // DPAD_MODE != DPAD_NONE

// ---------------------------------------------------------------------------
// MIDI callbacks
// ---------------------------------------------------------------------------
void onNoteOn(byte channel, byte note, byte velocity) {
#ifdef DEBUG_MIDI_MONITOR
  DBGF("note on  ch=%u note=%u vel=%u\n", channel, note, velocity);
#endif
  if (note >= 128) return;
  if (velocity < VELOCITY_GATE) return;   // a velocity-0 note-on is a note-off

  uint8_t z = noteZone[note];
  if (z == ZONE_NONE) {
    DBGF("unmapped note %u (vel %u) - add it to notemap.h\n", note, velocity);
    return;
  }
  triggerZone(z, velocity, millis());
}

void onControlChange(byte channel, byte control, byte value) {
#ifdef DEBUG_MIDI_MONITOR
  DBGF("cc       ch=%u num=%u val=%u\n", channel, control, value);
#endif
  // Never let the hi-hat pedal or mod wheel act as a button. The original
  // firmware reacted to *any* controller above a threshold, which on an Alesis
  // kit means the hi-hat pedal (CC#4, full 0-127 sweep) mashes Start.
  if (control == CC_ALWAYS_IGNORE_1) return;
  if (control == CC_ALWAYS_IGNORE_2) return;

#ifdef CC_START_CONTROLLER
  if (control == CC_START_CONTROLLER) {
    ccButtonPressed = (value >= CC_START_THRESHOLD);
  }
#else
  (void)value;
#endif
}

// ---------------------------------------------------------------------------
void setup() {
  DBG_BEGIN();
  pinMode(LEDPIN, OUTPUT);

#ifdef INPUTPIN
  pinMode(INPUTPIN, INPUT_PULLUP);
#endif

#if DPAD_MODE == DPAD_GPIO
  const uint8_t gpioPins[] = { DPAD_UP_PIN, DPAD_DOWN_PIN, DPAD_LEFT_PIN, DPAD_RIGHT_PIN,
                               DPAD_A_PIN, DPAD_B_PIN, DPAD_START_PIN, DPAD_SELECT_PIN };
  for (uint8_t i = 0; i < sizeof(gpioPins); i++) pinMode(gpioPins[i], INPUT_PULLUP);
#endif

  memset(zones, 0, sizeof(zones));
  buildNoteMap();

  // The USBHost examples recommend a pause before starting the host stack so
  // that the console finishes enumerating us first. The LED is on during it.
  digitalWrite(LEDPIN, HIGH);
  delay(1500);
  digitalWrite(LEDPIN, LOW);

#if DPAD_MODE == DPAD_QWSTPAD
  QWSTPAD_I2C_BUS.begin();
  QWSTPAD_I2C_BUS.setClock(QWSTPAD_I2C_CLOCK);
  if (qwstpad.begin()) {
    qwstpad.setLeds(0b0001);
    DBGLN("Qw/ST Pad found");
  } else {
    DBGF("Qw/ST Pad NOT found at 0x%02X - check wiring, 3.3V power, and the "
         "ADDR_SEL traces\n", QWSTPAD_I2C_ADDR);
  }
#endif

  usbHost.begin();
  midiInput.setHandleNoteOn(onNoteOn);
  midiInput.setHandleControlChange(onControlChange);

  DBGLN();
  DBGLN("teensympa - MIDI Pro Adapter");
  DBGF("  board       : %s\n", BOARD_NAME);
  DBGF("  note map    : %s\n", ACTIVE_MAP_NAME);
  DBGF("  note on time: %d ms (retrigger gap %d ms)\n", NOTE_ON_TIME, RETRIGGER_GAP_TIME);
  DBGLN();
}

// ---------------------------------------------------------------------------
void loop() {
  const uint32_t now = millis();

  usbHost.Task();
  // Drain every queued message. The original read one per loop iteration,
  // which meant a burst (a flam, or a chord across four pads) trickled out
  // over several iterations instead of landing in the same report.
  while (midiInput.read()) { }

#if DPAD_MODE != DPAD_NONE
  pollDpad(now);
#endif

  // ---- Which zones are currently on -------------------------------------
  bool on[ZONE_COUNT];
  for (uint8_t z = 0; z < ZONE_COUNT; z++) on[z] = zoneActive(z, now);

  const bool anyPad = on[ZONE_RED] || on[ZONE_YELLOW] || on[ZONE_BLUE] || on[ZONE_GREEN];
  const bool anyCym = on[ZONE_YELLOW_CYM] || on[ZONE_BLUE_CYM] || on[ZONE_GREEN_CYM];

  // ---- Start / Select ---------------------------------------------------
  bool startPressed  = ccButtonPressed;
  bool selectPressed = false;
  bool psPressed     = false;

#ifdef INPUTPIN
  startPressed |= (digitalRead(INPUTPIN) != HIGH);
#endif
#if DPAD_MODE != DPAD_NONE
  if (psButtonLatched) {
    psPressed = true;          // + and - held together; suppress both
  } else {
    startPressed  |= pad.start;
    selectPressed |= pad.select;
  }
#endif

  // ---- Build the report -------------------------------------------------
  usb_mpa_reset_packet();

  if (on[ZONE_KICK])   usb_mpa_set_button(MPA_BTN_KICK);
  if (on[ZONE_KICK2])  usb_mpa_set_button(MPA_BTN_KICK2);
  if (on[ZONE_RED])                            usb_mpa_set_button(MPA_BTN_CIRCLE);
  if (on[ZONE_YELLOW] || on[ZONE_YELLOW_CYM])  usb_mpa_set_button(MPA_BTN_TRIANGLE);
  if (on[ZONE_BLUE]   || on[ZONE_BLUE_CYM])    usb_mpa_set_button(MPA_BTN_SQUARE);
  if (on[ZONE_GREEN]  || on[ZONE_GREEN_CYM])   usb_mpa_set_button(MPA_BTN_CROSS);

  // The pad/cymbal discriminator flags. A face button pressed with neither
  // flag set reads as a plain gamepad button, which is exactly what the D-pad
  // wants and what a drum hit can never produce.
  if (anyPad) usb_mpa_set_button(MPA_BTN_PAD_FLAG);
  if (anyCym) usb_mpa_set_button(MPA_BTN_CYMBAL_FLAG);

  if (startPressed)  usb_mpa_set_button(MPA_BTN_START);
  if (selectPressed) usb_mpa_set_button(MPA_BTN_SELECT);
  if (psPressed)     usb_mpa_set_button(MPA_BTN_PS);

  // ---- Hat ---------------------------------------------------------------
  // The hat is overloaded: it is the D-pad, and it is also how the protocol
  // says WHICH cymbal was hit (up = yellow, down = blue, centred = green,
  // qualified by the cymbal flag). So when a cymbal is sounding the hat
  // belongs to the cymbal encoding and the D-pad has to wait ~25 ms.
  //
  // Getting this the other way round - letting a held D-pad direction win -
  // silently rewrites which cymbal the game thinks you hit.
  if (anyCym) {
    if      (on[ZONE_YELLOW_CYM]) usb_mpa_set_hat(MPA_HAT_UP);
    else if (on[ZONE_BLUE_CYM])   usb_mpa_set_hat(MPA_HAT_DOWN);
    else                          usb_mpa_set_hat(MPA_HAT_NEUTRAL); // green
  }
#if DPAD_MODE != DPAD_NONE
  else {
    usb_mpa_set_hat(dpadHat());
    if (pad.a) usb_mpa_set_button(MPA_BTN_CROSS);     // confirm
    if (pad.b) usb_mpa_set_button(MPA_BTN_CIRCLE);    // back
    if (pad.x) usb_mpa_set_button(MPA_BTN_SQUARE);
    if (pad.y) usb_mpa_set_button(MPA_BTN_TRIANGLE);
  }
#endif

  // ---- Velocity (experimental) -------------------------------------------
#ifdef ENABLE_VELOCITY_REPORT
  // MIDI velocity is 0-127; the report bytes are 0-255.
  if (on[ZONE_GREEN]  || on[ZONE_GREEN_CYM])
    usb_mpa_set_velocity(MPA_VEL_GREEN,  zones[on[ZONE_GREEN] ? ZONE_GREEN : ZONE_GREEN_CYM].velocity << 1);
  if (on[ZONE_RED])
    usb_mpa_set_velocity(MPA_VEL_RED,    zones[ZONE_RED].velocity << 1);
  if (on[ZONE_YELLOW] || on[ZONE_YELLOW_CYM])
    usb_mpa_set_velocity(MPA_VEL_YELLOW, zones[on[ZONE_YELLOW] ? ZONE_YELLOW : ZONE_YELLOW_CYM].velocity << 1);
  if (on[ZONE_BLUE]   || on[ZONE_BLUE_CYM])
    usb_mpa_set_velocity(MPA_VEL_BLUE,   zones[on[ZONE_BLUE] ? ZONE_BLUE : ZONE_BLUE_CYM].velocity << 1);
#endif

  // ---- Transmit -----------------------------------------------------------
  // No dirty flag to maintain: the driver compares against the last packet it
  // actually sent and stays quiet if nothing moved. That removes a whole class
  // of "we forgot to set kitDirty" bugs and costs a 27-byte memcmp.
  usb_mpa_send_if_changed();

  // ---- Indicators ---------------------------------------------------------
#ifdef BLINKY
  const bool anything = anyPad || anyCym || on[ZONE_KICK] || on[ZONE_KICK2] ||
                        startPressed || selectPressed || psPressed;
  digitalWrite(LEDPIN, anything ? HIGH : LOW);
#endif

#if DPAD_MODE == DPAD_QWSTPAD
  // LED 1 on the pad tracks USB enumeration, LED 2 mirrors drum activity.
  static uint8_t lastLeds = 0xFF;
  uint8_t leds = (usb_configuration ? 0b0001 : 0) | ((anyPad || anyCym) ? 0b0010 : 0);
  if (leds != lastLeds) { qwstpad.setLeds(leds); lastLeds = leds; }
#endif
}
