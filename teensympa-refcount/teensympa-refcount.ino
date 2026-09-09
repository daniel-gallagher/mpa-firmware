#include <USBHost_t36.h>
#include <Wire.h>

// This sketch requires the "MIDI Pro Adapter (MPA)" USB Type from cores-mpa
// (https://github.com/daniel-gallagher/cores-mpa). If usb_mpa_* is undeclared
// below, the cores-mpa platform is not installed or the wrong USB Type is selected.
#ifndef MPA_INTERFACE
  #error "Select Tools > USB Type > MIDI Pro Adapter (MPA) from the cores-mpa platform"
#endif

// Board detection - this firmware supports both Teensy 3.6 and Teensy 4.1
#if defined(__IMXRT1062__)
  #define TEENSY_41
  #pragma message "Compiling for Teensy 4.1"
#elif defined(__MK66FX1M0__)
  #define TEENSY_36
  #pragma message "Compiling for Teensy 3.6"
#else
  #warning "This firmware is designed for Teensy 3.6 or Teensy 4.1"
#endif

USBHost usbHost;
USBHub hubOne( usbHost );
USBHub hubTwo( usbHost );
MIDIDevice midiInput( usbHost );

// structure containing how many milliseconds each function is to be left 'on'.
// 'start'/'select' are handled slightly differently, but could be added here
struct kitstate {
  int rPad;
  int kick;
  int yPad;
  int yHat;
  int bPad;
  int bHat;
  int gPad;
  int gHat;
};

// D-pad state structure for Rock Band navigation
struct dpadstate {
  bool up;
  bool down;
  bool left;
  bool right;
  bool start;   // only driven by the I2C pad's '+' button
  bool select;  // only driven by the I2C pad's '-' button
};

// boolean flag indicating whether we need to send the kit state
// out to our connected device - set by noteon events, or an imminent
// timing out of a pad

bool kitDirty;

// some configuration
//  - the LEDPIN is the default for the T3.6 and T4.1 (both use pin 13)
//  - if INPUTPIN is defined, you can trigger a 'start' by shorting it to ground
//  - if CC_MAX is defined, 'start' will be triggered by any continuous controller (usually a hat pedal) exceeding that value
//    NOTE: on kits whose hi-hat pedal sends CC#4 (Alesis Nitro, Roland TD-1, ...) this means every pedal press is a Start
//    press. Comment CC_MAX out if you use the I2C pad's '+' button for Start instead.
//  - NOTE_ON_TIME indicates the minimum duration, in milliseconds, notes will be left on: we don't wait for noteoff here
//    this uses elapsed time based on millis() instead of an interrupt - make sure you reboot your adapter at least once every 50 days
//  - BLINKY, if defined, will flash the LEDPIN when any pad is "on"
//  - D-pad pins for Rock Band navigation (comment out to disable D-pad support)

#define LEDPIN 13
//#define INPUTPIN 0
//#define CC_MAX 0x5A
#define NOTE_ON_TIME 25
#define BLINKY 1

// Drum kit note map. The default table covers Roland V-Drums (TD-1 etc.).
// Define exactly one of the kit-specific options below to adjust it.
//  - ALESIS_NITRO: Alesis Nitro / Nitro Mesh factory map (adds hi-hat half-open 23
//    and splash 21 as yellow cymbal, and moves tom 3 rim 58 to the green pad)
//  - YAMAHA_DTX_502: the DTX 502 reverses the crash and ride numbers compared with the TD-1
#define ALESIS_NITRO
//#define YAMAHA_DTX_502
#if defined(ALESIS_NITRO) && defined(YAMAHA_DTX_502)
  #error "Define only one of ALESIS_NITRO or YAMAHA_DTX_502"
#endif

// D-pad configuration for Rock Band navigation
// Two modes supported:
// 1. GPIO mode: Direct pin connections (switches to ground)
// 2. I2C mode: Pimoroni Qw/ST Pad
#define DPAD_ENABLED 1
#ifdef DPAD_ENABLED
  // Choose D-pad mode: comment/uncomment one of these
  //#define DPAD_GPIO_MODE
  #define DPAD_I2C_PIMORONI

  // Compile-time check for mode configuration
  #if defined(DPAD_GPIO_MODE) && defined(DPAD_I2C_PIMORONI)
    #error "Cannot define both DPAD_GPIO_MODE and DPAD_I2C_PIMORONI. Choose only one."
  #endif
  #if !defined(DPAD_GPIO_MODE) && !defined(DPAD_I2C_PIMORONI)
    #error "Must define either DPAD_GPIO_MODE or DPAD_I2C_PIMORONI when DPAD_ENABLED is set."
  #endif

  #ifdef DPAD_GPIO_MODE
    // GPIO mode: pins connected to switches that short to ground when pressed
    #define DPAD_UP_PIN 14
    #define DPAD_DOWN_PIN 15
    #define DPAD_LEFT_PIN 16
    #define DPAD_RIGHT_PIN 17
  #endif

  #ifdef DPAD_I2C_PIMORONI
    // Pimoroni Qw/ST Pad: a TCA9555 16-bit I/O expander on the Qw/ST (I2C) bus.
    // Address is 0x21 by default; 0x23 / 0x25 / 0x27 via the cuttable traces on the back.
    // Register map and button bit numbers follow pimoroni/qwstpad-micropython.
    #define PIMORONI_PAD_I2C_ADDR 0x21
    #define TCA9555_REG_INPUT0    0x00
    #define TCA9555_REG_OUTPUT0   0x02
    #define TCA9555_REG_POLARITY0 0x04
    #define TCA9555_REG_CONFIG0   0x06
    // 16-bit input word bit numbers (port0 = bits 0-7, port1 = bits 8-15)
    #define PIMORONI_BTN_UP     1
    #define PIMORONI_BTN_LEFT   2
    #define PIMORONI_BTN_RIGHT  3
    #define PIMORONI_BTN_DOWN   4
    #define PIMORONI_BTN_MINUS  5
    #define PIMORONI_BTN_PLUS   11
    #define PIMORONI_BTN_B      12
    #define PIMORONI_BTN_Y      13
    #define PIMORONI_BTN_A      14
    #define PIMORONI_BTN_X      15
    // Map the pad's '+' to Start and '-' to Select (comment out to ignore them)
    #define DPAD_I2C_START_SELECT
    // How often to poll the pad (ms). The I2C read costs ~0.3 ms at 100 kHz, so
    // don't do it every loop or it adds latency to the MIDI path.
    #define DPAD_POLL_MS 5
    // If the pad is missing / unplugged, how often to look for it again (ms)
    #define DPAD_PROBE_MS 1000
    // Use the pad's four white LEDs as status indicators (comment out to leave them dark).
    // Left to right:
    //   1  kit connected    - a MIDI device has enumerated on the Teensy's host port
    //   2  console connected - the console / PC has configured us as a drum kit
    //   3  pedal is Start   - CC_MAX is defined, so the hi-hat pedal presses Start
    //   4  hit              - flashes for LED_HIT_MS whenever any pad is down
    #define DPAD_I2C_LEDS
    // LED bit numbers in the TCA9555 16-bit output word (active low)
    #define PIMORONI_LED1_BIT   6
    #define PIMORONI_LED2_BIT   7
    #define PIMORONI_LED3_BIT   9
    #define PIMORONI_LED4_BIT   10
    #define PIMORONI_LEDS_OFF   0x06C0  // all four LED bits high = dark, matches the init value
    #define LED_HIT_MS          60      // hit LED pulse; longer than NOTE_ON_TIME so it is visible
  #endif
#endif

// MPA report button numbers (bit positions in the 13 button field)
#define MPA_BTN_BLUE    0
#define MPA_BTN_GREEN   1
#define MPA_BTN_RED     2
#define MPA_BTN_YELLOW  3
#define MPA_BTN_KICK    4
#define MPA_BTN_SELECT  8
#define MPA_BTN_START   9
#define MPA_BTN_PAD     10  // "a drum pad was hit" flag
#define MPA_BTN_CYMBAL  11  // "a cymbal was hit" flag

// some program state
struct kitstate currentKitState;
bool inputPinState = false;
bool previousInputPinState = false;
unsigned long lastLoopTime;
#ifdef CC_MAX
bool continuousControllerPressed = false;
#endif
#ifdef DPAD_ENABLED
struct dpadstate currentDpadState;
struct dpadstate previousDpadState;
#endif
#ifdef DPAD_I2C_PIMORONI
bool dpadPresent = false;
unsigned long lastDpadPollTime = 0;
unsigned long lastDpadProbeTime = 0;
#endif
#ifdef DPAD_I2C_LEDS
uint16_t ledOutputWord = PIMORONI_LEDS_OFF;   // last value written to the output register
unsigned long hitLedUntil = 0;                 // millis() at which the hit LED goes dark
#endif


#ifdef DPAD_I2C_PIMORONI
// write one 16-bit register pair (port0 then port1) on the TCA9555
bool writePimoroniReg16( uint8_t reg, uint16_t value ) {
  Wire.beginTransmission( PIMORONI_PAD_I2C_ADDR );
  Wire.write( reg );
  Wire.write( (uint8_t)( value & 0xFF ) );
  Wire.write( (uint8_t)( value >> 8 ) );
  return Wire.endTransmission() == 0;
}

// Configure the pad the same way Pimoroni's own driver does:
//  - buttons as inputs, LED pins as outputs
//  - polarity inverted on the button pins so a pressed button reads as 1
//  - LEDs off
// Returns false if the pad did not acknowledge (not connected / wrong address).
bool initPimoroniPad() {
  if( !writePimoroniReg16( TCA9555_REG_CONFIG0,   0xF93F ) ) return false;
  if( !writePimoroniReg16( TCA9555_REG_POLARITY0, 0xF83F ) ) return false;
  if( !writePimoroniReg16( TCA9555_REG_OUTPUT0,   0x06C0 ) ) return false;
#ifdef DPAD_I2C_LEDS
  ledOutputWord = PIMORONI_LEDS_OFF;
#endif
  return true;
}

// Read the 16-bit button word. Returns false on any I2C error.
bool readPimoroniPad( uint16_t *buttons ) {
  Wire.beginTransmission( PIMORONI_PAD_I2C_ADDR );
  Wire.write( TCA9555_REG_INPUT0 );
  if( Wire.endTransmission( false ) != 0 ) return false;  // repeated start
  if( Wire.requestFrom( (uint8_t)PIMORONI_PAD_I2C_ADDR, (uint8_t)2 ) != 2 ) return false;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  *buttons = ( (uint16_t)hi << 8 ) | lo;
  return true;
}

// Poll the pad at DPAD_POLL_MS, re-probing at DPAD_PROBE_MS if it has gone away.
void updatePimoroniPad() {
  unsigned long now = millis();
  if( !dpadPresent ) {
    if( now - lastDpadProbeTime < DPAD_PROBE_MS ) return;
    lastDpadProbeTime = now;
    dpadPresent = initPimoroniPad();
    if( !dpadPresent ) {
      // no pad: make sure we are not holding any direction
      currentDpadState.up = currentDpadState.down = false;
      currentDpadState.left = currentDpadState.right = false;
      currentDpadState.start = currentDpadState.select = false;
      return;
    }
  }
  if( now - lastDpadPollTime < DPAD_POLL_MS ) return;
  lastDpadPollTime = now;

  uint16_t buttons;
  if( !readPimoroniPad( &buttons ) ) {
    // lost the pad - release everything and go back to probing
    dpadPresent = false;
    lastDpadProbeTime = now;
    buttons = 0;
  }
  currentDpadState.up    = ( buttons >> PIMORONI_BTN_UP ) & 1;
  currentDpadState.down  = ( buttons >> PIMORONI_BTN_DOWN ) & 1;
  currentDpadState.left  = ( buttons >> PIMORONI_BTN_LEFT ) & 1;
  currentDpadState.right = ( buttons >> PIMORONI_BTN_RIGHT ) & 1;
#ifdef DPAD_I2C_START_SELECT
  currentDpadState.start  = ( buttons >> PIMORONI_BTN_PLUS ) & 1;
  currentDpadState.select = ( buttons >> PIMORONI_BTN_MINUS ) & 1;
#endif
}

#ifdef DPAD_I2C_LEDS
// Refresh the four status LEDs. Only touches the bus when the pattern changes.
void updatePimoroniLeds( bool anyPadActive ) {
  if( !dpadPresent ) return;
  unsigned long now = millis();
  if( anyPadActive ) hitLedUntil = now + LED_HIT_MS;

  uint16_t word = PIMORONI_LEDS_OFF;                              // active low: clear a bit to light it
  if( midiInput ) word &= ~( 1 << PIMORONI_LED1_BIT );            // kit connected
  if( usb_configuration ) word &= ~( 1 << PIMORONI_LED2_BIT );    // console connected
#ifdef CC_MAX
  word &= ~( 1 << PIMORONI_LED3_BIT );                            // hi-hat pedal acts as Start
#endif
  if( (long)( hitLedUntil - now ) > 0 ) word &= ~( 1 << PIMORONI_LED4_BIT ); // hit

  if( word != ledOutputWord ) {
    if( writePimoroniReg16( TCA9555_REG_OUTPUT0, word ) ) ledOutputWord = word;
  }
}
#endif
#endif


void setup() {
#ifdef INPUTPIN
  pinMode( INPUTPIN, INPUT_PULLUP );
#endif
  pinMode( LEDPIN, OUTPUT );

#ifdef DPAD_ENABLED
  #ifdef DPAD_GPIO_MODE
    // Configure D-pad GPIO pins with internal pullups
    pinMode( DPAD_UP_PIN, INPUT_PULLUP );
    pinMode( DPAD_DOWN_PIN, INPUT_PULLUP );
    pinMode( DPAD_LEFT_PIN, INPUT_PULLUP );
    pinMode( DPAD_RIGHT_PIN, INPUT_PULLUP );
  #endif

  #ifdef DPAD_I2C_PIMORONI
    // Initialize I2C for Pimoroni Qw/ST Pad (pins 18 = SDA, 19 = SCL on both boards)
    Wire.begin();
    Wire.setClock( 100000 ); // 100kHz I2C
    dpadPresent = initPimoroniPad();
    lastDpadProbeTime = millis();
  #endif

  // Initialize D-pad state
  memset( &currentDpadState, 0, sizeof( currentDpadState ) );
  memset( &previousDpadState, 0, sizeof( previousDpadState ) );
#endif

  currentKitState.rPad = 0;
  currentKitState.kick = 0;
  currentKitState.yPad = 0;
  currentKitState.yHat = 0;
  currentKitState.bPad = 0;
  currentKitState.bHat = 0;
  currentKitState.gPad = 0;
  currentKitState.gHat = 0;
  kitDirty = false;
  lastLoopTime = millis();

  // advice in the api examples suggests pausing briefly before starting the userland usb subsystem,
  // to allow host enumeration to conclude first. we blink the led to show when initialisation delay has
  // completed

  digitalWrite( LEDPIN, HIGH );
  delay( 1500 );
  digitalWrite( LEDPIN, LOW );

  usbHost.begin();
  midiInput.setHandleNoteOn( onNoteOn );
#ifdef CC_MAX
  midiInput.setHandleControlChange( controlChange );
#endif
};

// if a kit has a positive time on remaining, reduce it by 'elapsed' milliseconds,
// but clamp the value at zero. set the kit dirty flag if any kit would hit zero.
void ageKitStates( int elapsed ) {
  if( ( currentKitState.rPad && currentKitState.rPad <= elapsed ) ||
      ( currentKitState.kick && currentKitState.kick <= elapsed ) ||
      ( currentKitState.yPad && currentKitState.yPad <= elapsed ) ||
      ( currentKitState.yHat && currentKitState.yHat <= elapsed ) ||
      ( currentKitState.bPad && currentKitState.bPad <= elapsed ) ||
      ( currentKitState.bHat && currentKitState.bHat <= elapsed ) ||
      ( currentKitState.gPad && currentKitState.gPad <= elapsed ) ||
      ( currentKitState.gHat && currentKitState.gHat <= elapsed ) )
      // at least one pad will age out in this cycle
      kitDirty = true;

  currentKitState.rPad = max( 0, currentKitState.rPad - elapsed );
  currentKitState.kick = max( 0, currentKitState.kick - elapsed );
  currentKitState.yPad = max( 0, currentKitState.yPad - elapsed );
  currentKitState.yHat = max( 0, currentKitState.yHat - elapsed );
  currentKitState.bPad = max( 0, currentKitState.bPad - elapsed );
  currentKitState.bHat = max( 0, currentKitState.bHat - elapsed );
  currentKitState.gPad = max( 0, currentKitState.gPad - elapsed );
  currentKitState.gHat = max( 0, currentKitState.gHat - elapsed );
}

void loop() {
  int elapsed;

  usbHost.Task();
  midiInput.read();

  // age kit states
  elapsed = millis() - lastLoopTime;
  ageKitStates( elapsed );

  // manage the "input pin", which sends a 'start' message
  inputPinState = false;
#ifdef INPUTPIN
  // if INPUTPIN is defined and the pin is pulled low,
  inputPinState |= ( digitalRead( INPUTPIN ) != HIGH );
#endif
#ifdef CC_MAX
  inputPinState |= continuousControllerPressed;
#endif

  if( inputPinState != previousInputPinState ) {
    kitDirty = true;
    previousInputPinState = inputPinState;
  }

#ifdef DPAD_ENABLED
  // Read D-pad state based on configured mode
  #ifdef DPAD_GPIO_MODE
    // GPIO mode: Read pins (active LOW - pressed when pin is LOW)
    currentDpadState.up = ( digitalRead( DPAD_UP_PIN ) == LOW );
    currentDpadState.down = ( digitalRead( DPAD_DOWN_PIN ) == LOW );
    currentDpadState.left = ( digitalRead( DPAD_LEFT_PIN ) == LOW );
    currentDpadState.right = ( digitalRead( DPAD_RIGHT_PIN ) == LOW );
  #endif

  #ifdef DPAD_I2C_PIMORONI
    // I2C mode: Read from Pimoroni Qw/ST Pad (rate limited inside)
    updatePimoroniPad();
  #endif

  // Check if D-pad state changed
  if( memcmp( &currentDpadState, &previousDpadState, sizeof( currentDpadState ) ) != 0 ) {
    kitDirty = true;
    previousDpadState = currentDpadState;
  }
#endif

  // if the kit is dirty, send the necessary reports
  if( kitDirty ) {
    usb_mpa_reset_packet();

    if( inputPinState ) usb_mpa_set_button( MPA_BTN_START );
#ifdef DPAD_ENABLED
    if( currentDpadState.start ) usb_mpa_set_button( MPA_BTN_START );
    if( currentDpadState.select ) usb_mpa_set_button( MPA_BTN_SELECT );
#endif
    if( currentKitState.kick ) usb_mpa_set_button( MPA_BTN_KICK );
    if( currentKitState.rPad ) usb_mpa_set_button( MPA_BTN_RED );
    if( currentKitState.yPad || currentKitState.yHat ) usb_mpa_set_button( MPA_BTN_YELLOW );
    if( currentKitState.bPad || currentKitState.bHat ) usb_mpa_set_button( MPA_BTN_BLUE );
    if( currentKitState.gPad || currentKitState.gHat ) usb_mpa_set_button( MPA_BTN_GREEN );
    if( currentKitState.rPad || currentKitState.yPad || currentKitState.bPad || currentKitState.gPad ) usb_mpa_set_button( MPA_BTN_PAD );
    if( currentKitState.yHat ) usb_mpa_set_hat( MPA_HAT_UP );
    if( currentKitState.bHat ) usb_mpa_set_hat( MPA_HAT_DOWN );
    if( currentKitState.yHat || currentKitState.bHat || currentKitState.gHat ) usb_mpa_set_button( MPA_BTN_CYMBAL );

#ifdef DPAD_ENABLED
    // Map D-pad to HAT control for Rock Band navigation
    // D-pad takes priority over drum cymbals only when actually pressed
    if( currentDpadState.up || currentDpadState.down || currentDpadState.left || currentDpadState.right ) {
      if( currentDpadState.up && currentDpadState.right ) {
        usb_mpa_set_hat( MPA_HAT_UP_RIGHT );
      } else if( currentDpadState.up && currentDpadState.left ) {
        usb_mpa_set_hat( MPA_HAT_UP_LEFT );
      } else if( currentDpadState.down && currentDpadState.right ) {
        usb_mpa_set_hat( MPA_HAT_DOWN_RIGHT );
      } else if( currentDpadState.down && currentDpadState.left ) {
        usb_mpa_set_hat( MPA_HAT_DOWN_LEFT );
      } else if( currentDpadState.up ) {
        usb_mpa_set_hat( MPA_HAT_UP );
      } else if( currentDpadState.down ) {
        usb_mpa_set_hat( MPA_HAT_DOWN );
      } else if( currentDpadState.left ) {
        usb_mpa_set_hat( MPA_HAT_LEFT );
      } else if( currentDpadState.right ) {
        usb_mpa_set_hat( MPA_HAT_RIGHT );
      }
    }
#endif

    usb_mpa_send();
    kitDirty = false;
  }

  // does any pad have time remaining on it?
  bool anyPadActive = currentKitState.rPad || currentKitState.kick ||
                      currentKitState.yPad || currentKitState.yHat ||
                      currentKitState.bPad || currentKitState.bHat ||
                      currentKitState.gPad || currentKitState.gHat;

#ifdef BLINKY
  digitalWrite( LEDPIN, ( anyPadActive || inputPinState ) ? HIGH : LOW );
#endif

#ifdef DPAD_I2C_LEDS
  updatePimoroniLeds( anyPadActive );
#endif

  lastLoopTime = millis();
}

// the constants used in the switch statement should support, at least,
// roland v-drums, the alesis nitro and the yamaha dtx 502
//
// Alesis Nitro factory map (Nitro user guide, "Pad MIDI Note Numbers"):
//   kick 36 | snare 38 rim 40 | tom1 48 rim 50 | tom2 45 rim 47 | tom3 43 rim 58 | tom4 41 rim 39
//   ride 51 | crash1 49 | crash2 57 | hi-hat open 46 half-open 23 closed 42 pedal 44 splash 21

void onNoteOn( byte channel, byte note, byte velocity ) {
  kitDirty = true;
  switch( note ) {

    case 27: case 31: case 34: case 37: case 38: case 39: case 40:
      currentKitState.rPad += NOTE_ON_TIME;
      break;

    case 35: case 36:
      currentKitState.kick += NOTE_ON_TIME;
      break;

    case 48: case 50:
      currentKitState.yPad += NOTE_ON_TIME;
      break;

#ifdef ALESIS_NITRO
    case 21: case 23:   // hi-hat splash, hi-hat half-open
#endif
    case 22: case 26: case 42: case 44: case 46: case 54: case 78: case 79:
    case 83: case 85: case 86:
      currentKitState.yHat += NOTE_ON_TIME;
      break;

    case 45: case 47:
      currentKitState.bPad += NOTE_ON_TIME;
      break;

#if defined(YAMAHA_DTX_502)
    case 59: case 49: case 55:
#elif defined(ALESIS_NITRO)
    case 51: case 53: case 59:  // 58 is tom 3 rim on the Nitro, see gPad
#else
    case 51: case 53: case 58: case 59:
#endif
      currentKitState.bHat += NOTE_ON_TIME;
      break;

#ifdef ALESIS_NITRO
    case 58:            // tom 3 rim
#endif
    case 43: case 41:
      currentKitState.gPad += NOTE_ON_TIME;
      break;

#ifdef YAMAHA_DTX_502
    case 51: case 52: case 53:
#else
    case 49: case 52: case 55: case 57:
#endif
      currentKitState.gHat += NOTE_ON_TIME;
      break;

  }
}

#ifdef CC_MAX
// this function reads all continuous controllers at once - not ideal, but
// I didn't want to have to deal with different kits defining them differently
void controlChange(byte channel, byte control, byte value) {
  if( value >= CC_MAX ) continuousControllerPressed = true;
  else continuousControllerPressed = false;
}
#endif
