// ---------------------------------------------------------------------------
// Pimoroni Qw/ST Pad driver (TCA9555 I/O expander), no external dependencies.
//
// Verified against Pimoroni's official MicroPython and Python drivers and the
// published board schematic:
//   https://github.com/pimoroni/qwstpad-micropython  (src/qwstpad.py)
//   https://github.com/pimoroni/qwstpad-python       (qwstpad/__init__.py)
//   Pimoroni_QwSTPad_Schematic.pdf
//
// Worth stating plainly, because a plausible-looking wrong version of this is
// easy to end up with: the pad is a TCA9555 at 0x21/0x23/0x25/0x27 (never
// 0x50 - that address is not even reachable on a TCA9555), you read TWO bytes
// starting at register 0x00, and the button bit order is
// UP=1, LEFT=2, RIGHT=3, DOWN=4 - not the intuitive UP/DOWN/LEFT/RIGHT.
// A naive bit0..bit3 guess gets LEFT and RIGHT right by luck and UP/DOWN
// wrong, which reads as "mostly working" on the bench.
//
// The driver programs the TCA9555 polarity-inversion registers exactly as
// Pimoroni's does, so button bits read 1 = pressed even though the switches
// are physically active-low. Do not invert again in your own code.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <Wire.h>

// TCA9555 command bytes. Registers are paired (0,1) (2,3) (4,5) (6,7); a
// 2-byte access starting at an even register hits that register then its
// partner, which is what makes the 16-bit reads and writes below work.
enum : uint8_t {
  TCA_INPUT0    = 0x00, TCA_INPUT1    = 0x01,
  TCA_OUTPUT0   = 0x02, TCA_OUTPUT1   = 0x03,
  TCA_POLARITY0 = 0x04, TCA_POLARITY1 = 0x05,
  TCA_CONFIG0   = 0x06, TCA_CONFIG1   = 0x07
};

// Button masks within the 16-bit word (port1 << 8 | port0), active HIGH after
// the polarity registers are programmed.
enum : uint16_t {
  QP_UP    = 0x0002,  // P0_1
  QP_LEFT  = 0x0004,  // P0_2
  QP_RIGHT = 0x0008,  // P0_3
  QP_DOWN  = 0x0010,  // P0_4
  QP_MINUS = 0x0020,  // P0_5   (silkscreen "-")
  QP_PLUS  = 0x0800,  // P1_3   (silkscreen "+")
  QP_B     = 0x1000,  // P1_4
  QP_Y     = 0x2000,  // P1_5
  QP_A     = 0x4000,  // P1_6
  QP_X     = 0x8000,  // P1_7
  // Every button bit, and nothing else. Bits 6, 7, 9 and 10 are the LED output
  // pins and bits 0 and 8 are unconnected package pins - P1_0 in particular is
  // not polarity-inverted and so reads 1 forever. Never test the raw word
  // against zero; always mask with QP_ALL first.
  //
  // 0xF83E == bits 15,14,13,12,11 (X,A,Y,B,+) and 5,4,3,2,1 (-,D,R,L,U),
  // which is exactly the set of pins the polarity registers invert, minus the
  // unconnected P0_0.
  QP_ALL   = 0xF83E
};

class QwSTPad {
public:
  QwSTPad(TwoWire &bus, uint8_t addr) : _bus(bus), _addr(addr) {}

  bool begin() {
    _bus.beginTransmission(_addr);
    if (_bus.endTransmission() != 0) { _present = false; return false; }
    // Exactly Pimoroni's init sequence.
    writeReg16(TCA_CONFIG0,   0xF93F); // 0x06=0x3F 0x07=0xF9 - button pins in, LED pins out
    writeReg16(TCA_POLARITY0, 0xF83F); // 0x04=0x3F 0x05=0xF8 - invert the button pins
    writeReg16(TCA_OUTPUT0,   0x06C0); // 0x02=0xC0 0x03=0x06 - all four LEDs off
    _present = true;
    _leds = 0;
    return true;
  }

  bool present() const { return _present; }

  // Raw button word, 1 = pressed. Returns 0 if the pad is missing or the
  // transfer fails, which reads as "nothing pressed" - the safe failure.
  uint16_t read() {
    if (!_present) return 0;
    return readReg16(TCA_INPUT0) & QP_ALL;
  }

  // bit0..bit3 -> LED 1..4, 1 = lit. LEDs are wired anode-to-3V3, so the
  // expander pin sinks current and a LOW bit turns the LED on.
  void setLeds(uint8_t leds) {
    if (!_present) return;
    static const uint16_t bits[4] = { 0x0040, 0x0080, 0x0200, 0x0400 };
    _leds = leds & 0x0F;
    uint16_t out = 0;
    for (uint8_t i = 0; i < 4; i++)
      if (!(_leds & (1 << i))) out |= bits[i];
    writeReg16(TCA_OUTPUT0, out);
  }

  // Try to re-establish contact with a pad that was unplugged. Cheap enough to
  // call once a second from the main loop.
  bool reconnect() { return begin(); }

private:
  void writeReg16(uint8_t reg, uint16_t v) {
    _bus.beginTransmission(_addr);
    _bus.write(reg);
    _bus.write((uint8_t)(v & 0xFF));        // low byte -> even register
    _bus.write((uint8_t)((v >> 8) & 0xFF)); // high byte -> odd register
    if (_bus.endTransmission() != 0) _present = false;
  }

  uint16_t readReg16(uint8_t reg) {
    _bus.beginTransmission(_addr);
    _bus.write(reg);
    if (_bus.endTransmission(false) != 0) { _present = false; return 0; } // repeated start
    if (_bus.requestFrom(_addr, (uint8_t)2) != 2) { _present = false; return 0; }
    uint8_t lo = _bus.read();   // Input Port 0
    uint8_t hi = _bus.read();   // Input Port 1
    return ((uint16_t)hi << 8) | lo;
  }

  TwoWire &_bus;
  uint8_t  _addr;
  bool     _present = false;
  uint8_t  _leds    = 0;
};
