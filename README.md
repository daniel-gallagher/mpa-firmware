# mpa-firmware

Arduino sketch for a **Teensy 4.1** (or Teensy 3.6) that turns a USB MIDI drum kit into a
Rock Band drum controller, using the "MIDI Pro Adapter (MPA)" USB Type from
[cores-mpa](https://github.com/daniel-gallagher/cores-mpa).

The Teensy's USB host port listens for a MIDI kit. Each note-on is looked up in a note map
and sets the matching MPA pad/cymbal for 25 ms (note-offs are ignored). The Teensy's device
port presents itself to the console as a MadCatz MIDI Pro Adapter.

## Features

- **Teensy 4.1 and 3.6** from one sketch (board is detected at compile time)
- **Alesis Nitro** note map (default), plus Roland V-Drums (TD-1 etc.) and Yamaha DTX 502
- **D-pad for menu navigation**, either
  - a **Pimoroni Qw/ST Pad** over I2C (default), whose `+` / `-` buttons also act as
    Start / Select and whose four LEDs show kit / console / pedal / hit status, or
  - four switches to ground on GPIO pins 14-17
- Start can also come from a switch on pin 0 or from any continuous controller above a
  threshold (i.e. the hi-hat pedal)
- LED on pin 13 blinks while any pad is held

## Requirements

- Arduino IDE 2.x (or arduino-cli) with **Teensyduino 1.62.0** installed via Boards Manager
- The **cores-mpa** platform installed with its `install-mpa-platform.ps1` script. It shows
  up as board package *Teensyduino (cores-mpa)* and provides the MPA USB Type
- USBHost_t36 and Wire (both ship with Teensyduino)

## Building and flashing

1. Open `teensympa-refcount/teensympa-refcount.ino`
2. **Tools > Board > Teensyduino (cores-mpa) > Teensy 4.1 (MPA)**
3. **Tools > USB Type > MIDI Pro Adapter (MPA)**
4. **Tools > CPU Speed > 600 MHz**
5. Upload. Because the MPA type has no serial port the loader cannot reboot the board
   itself: press the **program button** on the Teensy when Teensy Loader asks for it.

arduino-cli equivalent:

```bash
arduino-cli compile --fqbn teensy-mpa:avr:teensy41:usb=mpa,speed=600,opt=o2std,keys=en-us teensympa-refcount
```

See [TEENSY41_MIGRATION.md](TEENSY41_MIGRATION.md) for wiring, configuration and a
first-power-up test plan.

## Configuration

Everything is a `#define` near the top of the sketch:

| Define | Default | Purpose |
|---|---|---|
| `ALESIS_NITRO` | on | Alesis Nitro / Nitro Mesh factory note map |
| `YAMAHA_DTX_502` | off | Yamaha DTX 502 crash/ride swap |
| `DPAD_ENABLED` | on | Any D-pad support at all |
| `DPAD_I2C_PIMORONI` / `DPAD_GPIO_MODE` | I2C | Which D-pad hardware (pick one) |
| `PIMORONI_PAD_I2C_ADDR` | `0x21` | Qw/ST Pad address (`0x23`/`0x25`/`0x27` via rear traces) |
| `DPAD_I2C_START_SELECT` | on | Map the pad's `+` to Start and `-` to Select |
| `DPAD_I2C_LEDS` | on | Pad LEDs: kit connected, console connected, pedal-is-Start, hit flash |
| `LED_HIT_MS` | 60 | Length of the hit LED pulse |
| `CC_MAX` | `0x5A` | Any CC value at or above this presses Start (comment out to disable) |
| `INPUTPIN` | off | Pin that presses Start when shorted to ground |
| `NOTE_ON_TIME` | 25 | ms a hit stays "down" |
| `BLINKY` | on | LED feedback |
