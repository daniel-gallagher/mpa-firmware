// ---------------------------------------------------------------------------
// teensympa - configuration
//
// Everything you would normally want to change lives in this file.
// ---------------------------------------------------------------------------
#pragma once

// ===========================================================================
// Drum module profile
//
// Pick the one that matches the brain feeding MIDI into the Teensy's USB host
// port. See notemap.h for the actual note tables and where they came from.
// ===========================================================================
#define KIT_PROFILE_ROLAND        0   // Roland TD-1 / TD-4 / TD-17 (V-Drums)
#define KIT_PROFILE_YAMAHA_DTX    1   // Yamaha DTX502 and relatives
#define KIT_PROFILE_ALESIS        2   // Alesis Nitro / Nitro Max / Surge / DM7X
#define KIT_PROFILE_ALESIS_CMD    3   // Alesis Command / Command Mesh
#define KIT_PROFILE_ALESIS_CRIM   4   // Alesis Crimson II

#define KIT_PROFILE               KIT_PROFILE_ALESIS

// ===========================================================================
// Timing
// ===========================================================================

// How long a pad or cymbal is reported as "held" after a note-on, in ms.
// We never wait for a note-off - many modules do not send one, and the ones
// that do send it too late to be useful.
//
// Do not shorten this casually. The console samples the controller once per
// rendered frame (16.7 ms at 60 fps), so a pulse much under ~25 ms risks
// falling between two samples and being missed entirely. 25 ms is the value
// the original Teensy 3.6 build used and is known to work.
#define NOTE_ON_TIME              25

// When a second hit on the same zone arrives while it is still "held", we
// force the button off for this many milliseconds before turning it back on.
// Without this gap the console sees one long press instead of two hits, so
// fast rolls, flams and buzz strokes silently lose notes.
//
// Set to 0 to restore the old behaviour (no retrigger gap).
#define RETRIGGER_GAP_TIME        4

// Ignore repeat note-ons for the same zone that arrive within this many ms of
// the previous one. Mesh kick pedals and cheap piezo triggers can double-fire;
// this suppresses the phantom second hit. Set to 0 to disable.
#define TRIGGER_DEBOUNCE_TIME     6

// ===========================================================================
// Start / Select
//
// The original firmware treated ANY continuous controller with a value at or
// above CC_MAX as a Start press. That is actively dangerous on an Alesis kit:
// the hi-hat pedal streams CC#4 across the full 0-127 range, so pressing the
// hi-hat pedal would mash Start. Alesis Strike modules also use CC#118/#119
// for kit up/down.
//
// So the CC trigger is now opt-in AND restricted to one specific controller
// number. With a D-pad wired up you almost certainly want it off entirely.
// ===========================================================================

//#define CC_START_CONTROLLER     64   // e.g. a sustain pedal on CC#64
#define CC_START_THRESHOLD        0x5A

// Controller numbers that must never be treated as a button, whatever else is
// configured. CC#4 is the hi-hat pedal on essentially every module.
#define CC_ALWAYS_IGNORE_1        4    // foot controller / hi-hat position
#define CC_ALWAYS_IGNORE_2        1    // modulation

// Optional: short a digital pin to ground to press Start.
//#define INPUTPIN                0

// ===========================================================================
// D-pad / navigation controller
// ===========================================================================
#define DPAD_NONE                 0
#define DPAD_QWSTPAD              1   // Pimoroni Qw/ST Pad over I2C (TCA9555)
#define DPAD_GPIO                 2   // discrete switches to ground

#define DPAD_MODE                 DPAD_QWSTPAD

// --- Qw/ST Pad settings ---
// Valid addresses are 0x21 (default), 0x23, 0x25, 0x27, selected by cutting
// the ADDR_SEL traces on the back of the board. 0x50 is not a valid TCA9555
// address at all.
#define QWSTPAD_I2C_ADDR          0x21
#define QWSTPAD_I2C_BUS           Wire      // Teensy 4.1: Wire=18/19, Wire1=17/16, Wire2=25/24
#define QWSTPAD_I2C_CLOCK         400000    // TCA9555 tops out at 400 kHz
// Power the pad from 3.3 V. Its on-board 10k pull-ups tie SDA/SCL to whatever
// rail you feed it, and the Teensy 4.1 is NOT 5 V tolerant.

// How often to poll the pad, in ms. The I2C read costs ~115 us at 400 kHz.
#define DPAD_POLL_INTERVAL        4
// A button must read the same for this many consecutive polls to count.
// The TCA9555 has no debounce and neither do the tactile switches.
#define DPAD_DEBOUNCE_SAMPLES     2

// Hold + and - together for this long to send the PS / Home button.
#define PS_BUTTON_HOLD_TIME       600

// --- GPIO mode pins (only used when DPAD_MODE == DPAD_GPIO) ---
#define DPAD_UP_PIN               14
#define DPAD_DOWN_PIN             15
#define DPAD_LEFT_PIN             16
#define DPAD_RIGHT_PIN            17
#define DPAD_A_PIN                20
#define DPAD_B_PIN                21
#define DPAD_START_PIN            22
#define DPAD_SELECT_PIN           23

// ===========================================================================
// Velocity reporting (EXPERIMENTAL - off by default)
//
// The 27-byte MPA report has twelve vendor-defined bytes that a genuine PS3
// Rock Band kit uses to carry per-pad hit velocity. Rock Band 3 reads them for
// Pro Drums dynamics. Enabling this costs nothing, but the byte order below is
// an educated guess - I have not been able to verify which vendor byte belongs
// to which pad against a real adapter capture. If dynamics behave strangely,
// turn it off. If you do verify the order, please fix the indices in
// mpa_report.h and open a PR.
// ===========================================================================
//#define ENABLE_VELOCITY_REPORT  1

// Velocities below this are treated as ghost notes and ignored entirely.
#define VELOCITY_GATE             1

// ===========================================================================
// Diagnostics
//
// USB Serial does not exist when the board is built as USB Type: MPA - the
// USB port is pretending to be a drum adapter. Debug output therefore goes to
// Serial1, the hardware UART on pins 0 (RX) and 1 (TX). Hook up a USB-serial
// adapter, or build with USB Type: "MPA + Serial (debug)" and change
// DEBUG_PORT to Serial.
// ===========================================================================
//#define DEBUG_ENABLED           1
#define DEBUG_PORT                Serial1
#define DEBUG_BAUD                115200
// Print every incoming MIDI message. Very noisy; useful for discovering the
// note numbers your module actually sends.
//#define DEBUG_MIDI_MONITOR      1

#define LEDPIN                    13
#define BLINKY                    1   // flash the LED while any zone is active
