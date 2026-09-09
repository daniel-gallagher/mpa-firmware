# Teensy 4.1 Migration Guide

This documents the move of the MPA firmware from a Teensy 3.6 to a **Teensy 4.1**, the
D-pad addition, and how to set up the build environment. The same sketch still builds for
the 3.6.

## What changed for the 4.1

| | Teensy 3.6 | Teensy 4.1 |
|---|---|---|
| CPU | MK66FX1M0, Cortex-M4 @ 180 MHz | i.MX RT1062, Cortex-M7 @ 600 MHz |
| USB device port | 12 Mbit | 480 Mbit capable; cores-mpa forces 12 Mbit for the MPA type |
| USB host | 5-pin header, USBHost_t36 | 5-pin header, USBHost_t36 (same API) |
| I2C (Wire) | pins 18 (SDA) / 19 (SCL) | pins 18 (SDA) / 19 (SCL) |
| LED | 13 | 13 |
| I/O voltage | 3.3 V, 5 V tolerant | 3.3 V, **not** 5 V tolerant |

The sketch itself needed no board-specific code: it detects the board with `__IMXRT1062__`
/ `__MK66FX1M0__` only to print which one it is compiling for. The real port is in
**cores-mpa**, which did not have a Teensy 4 implementation of the MPA USB type before.
That now lives in `teensy4/usb_mpa.c` on the `teensyduino-1.62` branch of cores-mpa.

## Environment setup

1. Arduino IDE 2.x. Add `https://www.pjrc.com/teensy/package_teensy_index.json` to
   *Additional boards manager URLs* and install **Teensy 1.62.0** from Boards Manager.
2. Clone [cores-mpa](https://github.com/daniel-gallagher/cores-mpa), check out the
   `teensyduino-1.62` branch and run `install-mpa-platform.ps1` in PowerShell. This creates
   `Documents\Arduino\hardware\teensy-mpa\avr`, a copy of the stock platform with the MPA
   core files overlaid and the extra USB Type in the menu. The stock Teensy package under
   `Arduino15` is left untouched, so Boards Manager updates never remove the mod.
3. Restart the IDE. Select **Teensyduino (cores-mpa) > Teensy 4.1 (MPA)** and
   **USB Type > MIDI Pro Adapter (MPA)**.

If you update Teensyduino later, update cores-mpa to the matching branch first, then re-run
the install script.

## Flashing

- With the MPA USB Type there is no USB serial, so Teensy Loader cannot ask the board to
  reboot into the bootloader. Click Upload, then press the **program button** on the Teensy
  when the loader window says to.
- First flash of a brand-new 4.1: any USB Type works for the first upload since the board
  ships in bootloader mode; later uploads always need the button while running MPA firmware.
- The board also cannot print to the serial monitor. Use the pin 13 LED
  (1.5 s on at boot, blinks with pad hits) as the status indicator.

## D-pad

### Pimoroni Qw/ST Pad (default, `DPAD_I2C_PIMORONI`)

The pad is a TCA9555 16-bit I/O expander on the Qw/ST (I2C) connector. The firmware
configures it exactly like Pimoroni's own driver (buttons as inputs with inverted polarity
so pressed reads as 1, LED pins as outputs, LEDs off) and reads the two input registers.

```
Teensy 4.1               Qw/ST Pad
Pin 18 (SDA)  ---------  SDA
Pin 19 (SCL)  ---------  SCL
3.3V          ---------  3V3
GND           ---------  GND
```

- Address `0x21` by default; `0x23`, `0x25`, `0x27` by cutting the traces on the back
  (update `PIMORONI_PAD_I2C_ADDR`).
- The pad has pull-ups on the bus; a Qw/ST cable straight to the Teensy pins is enough.
- Button bit numbers in the 16-bit word: Up 1, Left 2, Right 3, Down 4, `-` 5, `+` 11,
  B 12, Y 13, A 14, X 15.
- **`+` presses Start and `-` presses Select** (`DPAD_I2C_START_SELECT`). A/B/X/Y are
  read but unused.
- The pad is polled every 5 ms (`DPAD_POLL_MS`), not every loop, so the I2C transaction
  does not add latency to MIDI handling. If the pad is missing or unplugged, the firmware
  releases all directions and re-probes once a second (`DPAD_PROBE_MS`).

#### Status LEDs (`DPAD_I2C_LEDS`)

The pad's four white LEDs (expander pins 6, 7, 9, 10, active low) are driven from the same
5 ms tick as the buttons. The output register is only written when the pattern changes, so
in normal play the bus is idle apart from the button poll. Left to right:

| LED | Meaning | Source |
|---|---|---|
| 1 | Kit connected | USBHost_t36 has enumerated a MIDI device on the host port |
| 2 | Console connected | the host has sent SET_CONFIGURATION (`usb_configuration` non-zero) |
| 3 | Pedal is Start | `CC_MAX` is defined, so the hi-hat pedal presses Start |
| 4 | Hit | any pad is down; pulse stretched to `LED_HIT_MS` (60 ms) so it is visible |

On the bench, LED 1 and 2 answer the two questions that matter first: is the Nitro
recognised, and has the PC/console accepted the adapter. Comment out `DPAD_I2C_LEDS` to
leave the LEDs dark.

### GPIO switches (`DPAD_GPIO_MODE`)

Four momentary switches from the pin to ground, internal pull-ups enabled:

| Direction | Pin |
|---|---|
| Up | 14 |
| Down | 15 |
| Left | 16 |
| Right | 17 |

### Behaviour

D-pad directions drive the MPA hat switch, including diagonals when two adjacent buttons
are held. While any direction is held it overrides the hat values the yellow/blue cymbals
would otherwise produce, so menus can be navigated without accidental cymbal input.

## Alesis Nitro

`ALESIS_NITRO` is on by default. The Nitro's factory map (from the Nitro user guide, "Pad
MIDI Note Numbers"):

| Pad | Note | Rock Band |
|---|---|---|
| Kick | 36 | Kick |
| Snare / rim | 38 / 40 | Red |
| Tom 1 / rim | 48 / 50 | Yellow pad |
| Tom 2 / rim | 45 / 47 | Blue pad |
| Tom 3 / rim | 43 / 58 | Green pad |
| Hi-hat open / half-open / closed / pedal / splash | 46 / 23 / 42 / 44 / 21 | Yellow cymbal |
| Ride | 51 | Blue cymbal |
| Crash 1 / Crash 2 | 49 / 57 | Green cymbal |

Compared with the Roland table, the Nitro option adds notes 21 and 23 to the yellow cymbal
and moves 58 from the blue cymbal to the green pad.

**Hi-hat pedal:** the Nitro sends note 44 *and* continuous controller 4 when the pedal is
pressed. With `CC_MAX` defined (the default, carried over from the 3.6 firmware) a pedal
press therefore also presses **Start**, which pauses a song. If that is not what you want,
comment out `CC_MAX` and use the Qw/ST Pad's `+` for Start.

## Bench test plan

1. **Flash** with the MPA USB type. LED lights for 1.5 s at boot.
2. **Plug into a PC first.** It should enumerate as "Harmonix Drum kit for PlayStation(R)3"
   (VID 12BA, PID 0218) and show as a game controller with 13 buttons and a hat; pad LED 2
   lights once the PC has configured it. Windows
   *Set up USB game controllers* is enough to see buttons and hat move.
3. **Connect the Nitro** to the host port. Pad LED 1 should light. Hit each pad and cymbal; check the pin 13 LED and pad LED 4 blink
   and the right controller button lights (kick = button 5, red 3, yellow 4, blue 1,
   green 2 in Windows' 1-based numbering).
4. **D-pad.** Press each direction and diagonals and watch the hat; `+` / `-` should show
   as buttons 10 / 9.
5. **Hi-hat pedal.** Confirm whether you want it to press Start (see above).
6. **Console.** Plug into the console and check it is recognised as a drum kit.
   If it is not, the first thing to try is letting the port run at 480 Mbit: add
   `-DMPA_ALLOW_HIGH_SPEED` to the build (or edit `usb.c` in cores-mpa) and re-flash.

## Troubleshooting

- **`usb_mpa_reset_packet was not declared`** – the MPA USB Type is not selected, or the
  cores-mpa platform is not installed. Board must be the *(MPA)* one from the
  *Teensyduino (cores-mpa)* package.
- **D-pad does nothing** – run an I2C scanner sketch (with a normal USB Type so you have a
  serial monitor); the pad must answer at `0x21`. Check the Qw/ST cable orientation.
- **Every hi-hat press pauses the game** – comment out `CC_MAX`.
- **Kit not detected on the host port** – power the kit on before the Teensy, or try a
  powered hub; the 4.1's host port supplies limited current.
