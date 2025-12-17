# Teensy 4.1 Migration Guide

## Overview

This document describes the changes made to port the MPA firmware from Teensy 3.6 to **Teensy 4.1**, and documents the addition of D-pad support for Rock Band gameplay.

The firmware now automatically detects which board it's running on and works identically on both Teensy 3.6 and Teensy 4.1 platforms.

## Hardware Compatibility

### Teensy 4.1 vs Teensy 3.6

| Feature | Teensy 3.6 | Teensy 4.1 | Notes |
|---------|-----------|-----------|-------|
| CPU | ARM Cortex-M4 @ 180 MHz | ARM Cortex-M7 @ 600 MHz | Significantly faster |
| RAM | 256 KB | 1024 KB | More memory available |
| USB Host | Yes (via 5-pin header) | Yes (via 5-pin header) | Same interface |
| LED Pin | 13 | 13 | No change needed |
| Voltage | 3.3V I/O | 3.3V I/O | Compatible |
| Libraries | USBHost_t36 | USBHost_t36 | Same library works on both |

### Key Differences

1. **Processor Architecture**: Teensy 4.1 uses NXP i.MX RT1062 (ARM Cortex-M7) instead of MK66FX1M0 (ARM Cortex-M4)
2. **Performance**: Teensy 4.1 is approximately 3x faster
3. **Pin Compatibility**: Most digital pins work identically, but internal peripherals may differ
4. **Timing**: The firmware uses `millis()` for timing which works identically on both boards

## Code Changes

### Board Detection

The firmware now includes automatic board detection:

```cpp
#if defined(__IMXRT1062__)
  #define TEENSY_41
#elif defined(__MK66FX1M0__)
  #define TEENSY_36
#endif
```

This allows the same code to work on both platforms without modification.

### Library Compatibility

- **USBHost_t36**: This library works on both Teensy 3.6 and 4.x boards
- **cores-mpa**: The custom MPA USB type must support Teensy 4.1 (ensure you have the latest version)

### Pin Assignments

The following pins are used by the firmware:

| Function | Pin | Notes |
|----------|-----|-------|
| LED | 13 | Built-in LED (same on both boards) |
| Start/Select Input | 0 | Optional, disabled by default |
| D-pad Up | 14 | New feature, see D-pad section |
| D-pad Down | 15 | New feature, see D-pad section |
| D-pad Left | 16 | New feature, see D-pad section |
| D-pad Right | 17 | New feature, see D-pad section |

## D-pad Support

### Overview

D-pad support has been added for Rock Band navigation and menu control. The D-pad uses 4 digital input pins configured with internal pull-up resistors.

### Hardware Connection

Each D-pad direction requires:
1. A momentary switch (pushbutton or arcade button)
2. Connection between the assigned pin and ground when pressed

**Wiring Diagram:**

```
Teensy 4.1          Switch          Ground
Pin 14 (Up)    ----[SWITCH]----    GND
Pin 15 (Down)  ----[SWITCH]----    GND
Pin 16 (Left)  ----[SWITCH]----    GND
Pin 17 (Right) ----[SWITCH]----    GND
```

### Pin Configuration

The D-pad pins are configured with internal pull-ups, so they read HIGH when not pressed and LOW when the button is pressed (shorting to ground).

Default pin assignments:
- **Up**: Pin 14
- **Down**: Pin 15
- **Left**: Pin 16
- **Right**: Pin 17

### Customizing D-pad Pins

To change D-pad pin assignments, edit these defines in the firmware:

```cpp
#ifdef DPAD_ENABLED
  #define DPAD_UP_PIN 14
  #define DPAD_DOWN_PIN 15
  #define DPAD_LEFT_PIN 16
  #define DPAD_RIGHT_PIN 17
#endif
```

To disable D-pad support entirely, comment out or remove:

```cpp
#define DPAD_ENABLED 1
```

### D-pad Behavior

The D-pad is mapped to the MPA HAT control, which Rock Band uses for menu navigation:

- **Single directions**: Up, Down, Left, Right
- **Diagonal directions**: Up-Left, Up-Right, Down-Left, Down-Right (when two adjacent buttons pressed)

The D-pad takes priority over cymbal hat controls when pressed, allowing you to navigate menus without triggering unwanted drum hits.

## Installation Instructions

### Prerequisites

1. **Arduino IDE** (version 1.8.19 or newer) or **Teensyduino**
2. **Teensyduino** add-on installed (https://www.pjrc.com/teensy/td_download.html)
3. **USBHost_t36** library (included with Teensyduino)
4. **cores-mpa** custom USB type (https://github.com/curiousjp/cores-mpa)

### Installing cores-mpa for Teensy 4.1

1. Download or clone the cores-mpa repository
2. Follow the installation instructions in the cores-mpa README
3. Ensure it includes Teensy 4.1 support (check for `__IMXRT1062__` definitions)
4. Restart Arduino IDE after installation

### Compiling and Uploading

1. Open `teensympa-refcount.ino` in Arduino IDE
2. Select your board:
   - **Tools → Board → Teensyduino → Teensy 4.1** (or Teensy 3.6)
3. Select USB Type:
   - **Tools → USB Type → MIDI Pro Adapter (MPA)**
4. Select CPU Speed (recommended):
   - **Tools → CPU Speed → 600 MHz** (for Teensy 4.1)
   - **Tools → CPU Speed → 180 MHz** (for Teensy 3.6)
5. Connect your Teensy via USB
6. Click **Upload** (or Sketch → Upload)

The Arduino IDE will compile the firmware and automatically detect which board you selected.

### Verification

After uploading:

1. The built-in LED (pin 13) will turn on for 1.5 seconds during initialization
2. Watch the Arduino IDE console for board detection messages:
   - "Compiling for Teensy 4.1" or "Compiling for Teensy 3.6"
3. The LED will blink when drum pads are hit (if BLINKY is enabled)

## Usage

### Connecting a MIDI Drum Kit

1. Connect your MIDI drum brain to the Teensy's USB host port using:
   - USB host cable (5-pin header to USB-A adapter)
   - Your drum kit's USB cable
2. Power the Teensy from your computer or game console
3. The Teensy acts as a MIDI Pro Adapter, translating drum hits to MPA button presses

### Rock Band Integration

1. Connect the Teensy (configured as MPA) to your game console's USB port
2. The game will recognize it as a MIDI Pro Adapter
3. Use drum pads for gameplay
4. Use the D-pad for menu navigation
5. Use start/select button (pin 0 or hat pedal) for game control

### Supported Drum Kits

The firmware includes MIDI note mappings for:
- **Roland V-Drums** (TD-1, TD-17, TD-27, etc.)
- **Yamaha DTX** series (enable `YAMAHA_DTX_502` define for DTX-502)

To support other drum kits, modify the MIDI note mappings in the `onNoteOn()` function.

## Troubleshooting

### Firmware Won't Compile

- Ensure Teensyduino is properly installed
- Verify cores-mpa is installed and supports your Teensy version
- Check that USBHost_t36 library is available
- Make sure you selected the correct board in Tools → Board

### D-pad Not Working

- Verify pin connections (should connect to ground when pressed)
- Check that `DPAD_ENABLED` is defined (not commented out)
- Ensure switches are connected to the correct pins
- Test with a multimeter: pins should read ~3.3V when open, ~0V when pressed

### USB Host Not Detecting Drum Kit

- Check USB host cable connection (5-pin header to USB-A)
- Ensure drum kit is powered on before the Teensy initializes
- Try unplugging and replugging the drum kit
- Some drum kits may require a powered USB hub

### LED Not Blinking

- Verify `BLINKY` is defined in the code
- Check that drum kit is sending MIDI notes
- Test with a different MIDI device to rule out kit issues

### Game Console Doesn't Recognize Adapter

- Ensure "MIDI Pro Adapter (MPA)" is selected in Tools → USB Type
- Verify cores-mpa is properly installed
- Some consoles may require specific MPA firmware versions

## Configuration Options

The following defines can be modified in the firmware:

| Define | Default | Description |
|--------|---------|-------------|
| `LEDPIN` | 13 | Built-in LED pin |
| `INPUTPIN` | undefined | Optional start/select button (short to ground) |
| `CC_MAX` | 0x5A | CC threshold for hat pedal start/select |
| `NOTE_ON_TIME` | 25 | Minimum pad "on" duration in milliseconds |
| `BLINKY` | 1 | Enable LED blinking when pads are active |
| `DPAD_ENABLED` | 1 | Enable D-pad support |
| `DPAD_UP_PIN` | 14 | D-pad up button pin |
| `DPAD_DOWN_PIN` | 15 | D-pad down button pin |
| `DPAD_LEFT_PIN` | 16 | D-pad left button pin |
| `DPAD_RIGHT_PIN` | 17 | D-pad right button pin |
| `YAMAHA_DTX_502` | undefined | Use Yamaha DTX-502 note mappings |

## Technical Details

### Timing Behavior

The firmware uses `millis()` for timing instead of hardware interrupts:
- More portable across Teensy versions
- No interrupt priority conflicts
- Simpler code maintenance
- **Important**: Reboot the adapter at least once every 50 days to prevent `millis()` overflow issues

### MIDI Note Mapping

Drum pads are mapped to MPA buttons as follows:

| Drum Pad | MPA Button | Notes |
|----------|-----------|-------|
| Kick | 4 | Bass drum pedal |
| Red Pad | 2 | Snare |
| Yellow Pad/Cymbal | 3 | Tom or Hi-hat |
| Blue Pad/Cymbal | 0 | Tom or Crash |
| Green Pad/Cymbal | 1 | Tom or Ride |

### HAT Control

The MPA HAT is used for:
- Yellow cymbal → HAT UP
- Blue cymbal → HAT DOWN  
- D-pad directions → HAT UP/DOWN/LEFT/RIGHT (takes priority when pressed)

## References

- [Teensy 4.1 Documentation](https://www.pjrc.com/store/teensy41.html)
- [cores-mpa Repository](https://github.com/curiousjp/cores-mpa)
- [USBHost_t36 Library](https://github.com/PaulStoffregen/USBHost_t36)
- [Original MPA Firmware](https://github.com/daniel-gallagher/mpa-firmware)

## License

This firmware maintains the same license as the original mpa-firmware project.

## Contributing

Improvements and bug reports are welcome! Please submit issues or pull requests to the GitHub repository.

---

**Last Updated**: December 2025
