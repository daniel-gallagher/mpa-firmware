# mpa-firmware

This Arduino sketch is firmware for a **Teensy 3.6** or **Teensy 4.1** using the MPA USB Type added by [cores-mpa](https://github.com/curiousjp/cores-mpa).

## Features

- **Dual Board Support**: Works on both Teensy 3.6 and Teensy 4.1 with automatic board detection
- **MIDI Drum Kit Translation**: Listens for MIDI input from drum kits and translates to MPA button presses
- **D-pad Support**: Added support for a 4-direction D-pad for Rock Band menu navigation
- **Flexible Input**: Start/Select can be triggered via digital pin or continuous controller (hat pedal)
- **Drum Kit Compatibility**: Includes note mappings for Roland V-Drums and Yamaha DTX series

## Quick Start

The firmware listens for a MIDI kit on the Teensy's USB host port. When it receives a MIDI message, it compares the note number to a list of mappings and sets the appropriate MPA pad. Pads automatically unset 25ms later (doesn't wait for note-off messages).

Start/Select can be pressed either by:
- Shorting digital pin zero to ground using a switch, or
- Sending any continuous control message (such as via a hat pedal) with a value of more than 0x5A

## New in This Version

✨ **Teensy 4.1 Support**: Full compatibility with Teensy 4.1 hardware  
✨ **D-pad Navigation**: Added 4-direction D-pad support for Rock Band menus  
✨ **Improved Documentation**: Comprehensive migration guide and pinout information

## Documentation

For detailed information about Teensy 4.1 porting, D-pad setup, and installation instructions, see:

📖 **[TEENSY41_MIGRATION.md](TEENSY41_MIGRATION.md)** - Complete migration guide with:
- Hardware compatibility details
- D-pad wiring instructions
- Pin assignments and customization
- Installation and compilation guide
- Troubleshooting tips

## Configuration

Most settings can be customized via defines in the code:
- Note-on duration timing
- MIDI note mappings (supports Yamaha DTX 502 with a define)
- D-pad pin assignments
- LED blink behavior
- Start/Select input options