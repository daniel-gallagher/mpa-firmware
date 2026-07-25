# mpa-firmware

A DIY replacement for the Mad Catz **MIDI Pro Adapter** — the discontinued box
that let you play Rock Band with a real electronic drum kit. A Teensy listens
to your drum module over USB-MIDI and presents itself to the console as a
Harmonix PS3 Rock Band drum controller.

This is a Teensy 4.1 port and rework of [curiousjp's original Teensy 3.6
firmware](https://oldarchive.progsoc.org/~curious/rock_band/diy_midi_pro_adapter2.html),
with an added D-pad for menu navigation.

```
  drum module ──USB-MIDI──▶ Teensy 4.1 USB host port
                                  │
  Qw/ST Pad ─────I2C──────▶       │
                                  ▼
                            Teensy 4.1 USB device port ──▶ console
                            (enumerates as 12BA:0218)
```

## Read this first: the core has to be patched

The MPA is not a USB type Teensyduino ships with. It is added by modifying the
Teensy core — a new HID device type with the exact descriptors of a real
Mad Catz adapter.

The original core fork, [`curiousjp/cores-mpa`](https://github.com/curiousjp/cores-mpa),
**only ever patched `teensy3/`**. There is no `teensy4/usb_mpa.c` in it, no
`USB_MPA` block in `teensy4/usb_desc.h`, and its Teensy 3.x implementation is
built on the `usb_malloc()` / `usb_tx()` packet-pool API that does not exist on
Teensy 4 at all. It is also pinned to a 2019 upstream and cannot be merged
forward.

So switching boards is not a sketch edit. `core-patch/` and
`tools/install_mpa_core.py` in this repo are a fresh Teensy 4.x implementation
written against the current upstream core.

## Install

1. Install Arduino IDE + Teensyduino, and the
   [USBHost_t36](https://github.com/PaulStoffregen/USBHost_t36) library.
2. Patch the core:

   ```
   python3 tools/install_mpa_core.py
   ```

   It finds your Teensyduino install, backs every file up to `*.mpa-orig`, and
   applies anchored, idempotent edits. Re-running it is a no-op;
   `--uninstall` reverses it exactly; `--check` shows what it would do without
   writing. Pass `--core-dir` if auto-detection misses.

3. Restart the IDE. **Tools → USB Type** now has:
   - **MPA (Rock Band drum adapter)** — what you flash for real use.
   - **MPA + Serial (debug)** — adds an emulated serial port so `Serial`,
     the Serial Monitor and Teensy Loader auto-reboot work. Adds a second USB
     interface the genuine adapter does not have, so don't use it on console.

4. Edit `firmware/teensympa/config.h` (drum module profile, D-pad type), open
   `firmware/teensympa/teensympa.ino`, select Teensy 4.1, and upload.

   With plain `USB_MPA` there is no serial port, so Teensy Loader can't reboot
   the board for you — press the program button on each upload. That's normal.

## What changed from the Teensy 3.6 version

### The USB device port

The 3.x driver allocated a `usb_packet_t` from a shared pool and handed it to
`usb_tx()`. Teensy 4 has no such pool: each driver owns static transfer
descriptors and DMA buffers, arms them with `usb_prepare_transfer()`, and
submits with `usb_transmit()`. The rewrite in `core-patch/teensy4/usb_mpa.c`
does that, and adds the thing that has no Teensy 3 equivalent —
`arm_dcache_flush_delete()` on the TX buffer before submitting. The IMXRT1062
has a data cache and the USB controller DMAs out of physical memory; skip the
flush and you get the classic "works on a 3.6, sends garbage on a 4.1".

Other things the port had to deal with, all documented in
[`TEENSY41_MIGRATION.md`](TEENSY41_MIGRATION.md):

- The 3.x timeout used a busy-loop constant table that stopped at 256 MHz — it
  would not have compiled at the Teensy 4's 600 MHz. Now it uses
  `systick_millis_count`.
- Endpoint 1 is unusable on Teensy 4 (`usb_config_tx()` rejects `ep < 2`, and
  `usb.c` scribbles magic values over EP1's queue heads after
  SET_CONFIGURATION). The MPA uses EP2 IN / EP3 OUT instead of the genuine
  device's EP1/EP2. Hosts read endpoint addresses from the descriptor, so this
  is invisible.
- Teensy 4 emits **two** config descriptors (high and full speed). Both carry
  the MPA block.
- The device is forced to full speed, like the real adapter. At high speed
  `bInterval` means 125 µs microframes, not milliseconds.
- The 3.x fork's `MPA_DESC_OFFSET` was `9`, which pointed at the *interface*
  descriptor rather than the HID descriptor. The port extends the core's own
  position-macro chain so the offset is right by construction (verified: 18).
- The 3.x fork set `iSerialNumber = 0` unconditionally, breaking serial-number
  port identification for every other USB type built from that core. Here it's
  conditional on `MPA_INTERFACE`.

### Firmware bugs fixed

- **The hi-hat pedal pressed Start.** The old code treated *any* continuous
  controller above `0x5A` as a Start press. Alesis modules stream the hi-hat
  pedal on CC#4 across the full 0–127 range, so riding the hi-hat mashed Start.
  CC-as-button is now opt-in and bound to one specific controller number, with
  CC#4 and CC#1 hard-blocked.
- **Alesis tom 3 rim fired a cymbal.** The old note map was a union of several
  modules' note numbers in one switch statement. Note 58 is the ride on a
  Roland but the tom 3 rim on an Alesis, and the shared table sent it to the
  blue cymbal. Notes 39, 23 and 21 (tom 4 rim, half-open hi-hat, splash) were
  unmapped entirely. Each module now gets its own table.
- **Fast rolls lost notes.** Every hit did `state += NOTE_ON_TIME`, so a roll
  pushed the counter up without bound and the button never went low — the
  console saw one long press. Hits now re-arm an absolute deadline, and a hit
  arriving while the zone is still held forces a short off-gap so the console
  sees a fresh edge.
- **`millis()` rollover.** The old file carried a comment telling you to reboot
  the adapter every 50 days. Deadlines compared with signed differences are
  correct across the wrap; there's a test for it.
- **The D-pad fought the cymbals.** The hat switch is overloaded — it is the
  D-pad *and* it is how the protocol says which cymbal you hit (up = yellow,
  down = blue, centred = green). A previous attempt gave the D-pad priority,
  which silently rewrites which cymbal the game thinks you played. Cymbals now
  own the hat while they're sounding.
- **The Qw/ST Pad register map was fabricated.** A previous attempt used
  address `0x50` (not a reachable TCA9555 address at all) and bit masks
  `UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08`. The real device is a TCA9555 at
  `0x21`/`0x23`/`0x25`/`0x27` with `UP=bit1, LEFT=bit2, RIGHT=bit3, DOWN=bit4`
  — the naive guess gets LEFT and RIGHT right by luck and UP/DOWN wrong. It
  also called constants (`MPA_HAT_UP_LEFT` and friends) that did not exist in
  the 3.x header, so it would not have compiled.

### New

- **D-pad support** — Pimoroni Qw/ST Pad over I2C, or discrete switches on
  GPIO. Full 8-direction hat, A/B/X/Y as the four face buttons, `+`/`-` as
  Start/Select, and both held together for the PS/Home button. Face buttons are
  sent *without* the pad/cymbal discriminator flags, so the game reads them as
  gamepad presses rather than drum hits — something the drums physically can't
  produce.
- **Module profiles** — Roland, Yamaha DTX, Alesis Nitro/Surge/DM7X, Alesis
  Command, Alesis Crimson II. See [`docs/NOTE_MAPS.md`](docs/NOTE_MAPS.md).
- **Debug output on Serial1** (pins 0/1), since USB serial doesn't exist in MPA
  mode. Includes a MIDI monitor and a warning for unmapped notes, which is the
  fastest way to profile a module nobody has documented.
- **Trigger debounce** for mesh kick pedals that double-fire.
- **Host-side test suite** — `test/run_tests.sh`. The .ino is plain C++, so
  with a simulated TCA9555 (one that honours the polarity registers) and a
  capture-only USB driver, the whole state machine runs on a PC. 40 assertions
  covering the note map, hat arbitration, retrigger behaviour and rollover.
  Two of the bugs listed above were caught by these tests, not by reading.

## Layout

```
firmware/teensympa/     the sketch - config.h, notemap.h, qwstpad.h, .ino
core-patch/teensy4/     new core files (usb_mpa.c / usb_mpa.h)
tools/install_mpa_core.py   idempotent, reversible core patcher
test/                   host-side test suite
docs/                   hardware, protocol and note-map references
teensympa-refcount.ino  the original Teensy 3.6 sketch, kept for reference
```

## Status

The core port is compile-verified for both `USB_MPA` and `USB_MPA_SEREMU` with
`arm-none-eabi-gcc` targeting cortex-m7, and the emitted descriptors were
dumped from the object file and checked byte for byte (`wTotalLength` 41 = array
length, HID descriptor at offset 18, `wDescriptorLength` 137 = report descriptor
size, endpoints `0x82`/`0x03`, VID/PID `12BA:0218`). The firmware logic passes
its host-side tests.

**It has not yet been tested against a console.** If enumeration fails, the
first things to try are in [`docs/PROTOCOL.md`](docs/PROTOCOL.md#if-the-console-rejects-the-device).
