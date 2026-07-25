# Hardware

## Teensy 4.1 USB host port

The drum module plugs into the Teensy's **USB host** header — the 5-pin row of
through-holes near the SD card slot, not the micro-USB connector. You need the
PJRC USB host cable (or your own: 5V, D−, D+, GND, plus the shield pad).

The host port's 5 V comes from `VUSB`, so the Teensy must be powered from its
own USB port (which it is, since that's the console connection). A drum module
is self-powered and draws essentially nothing over USB.

Two `USBHub` objects are declared in the sketch. Alesis explicitly recommend
*not* putting a hub between the module and the host, so this is only there so a
kit that presents behind an internal hub still enumerates.

Alesis modules are USB class compliant — no driver, no special handling. Note
that only MIDI travels over USB on these; there's no audio interface to confuse
the enumeration.

## Pimoroni Qw/ST Pad

A TCA9555 I/O expander with 10 buttons (D-pad, A/B/X/Y, `+`, `-`) and 4 LEDs on
a Qwiic/STEMMA-QT connector.

### Wiring

| Qw/ST Pad | Teensy 4.1 |
|---|---|
| SDA | pin 18 |
| SCL | pin 19 |
| 3V3 | **3.3 V** |
| GND | GND |

**Power it from 3.3 V, not 5 V.** The pad has 10 kΩ I2C pull-ups to whatever
rail you feed it, and the Teensy 4.1's I/O is *not* 5 V tolerant. Feeding the
pad 5 V pulls SDA/SCL to 5 V and damages the Teensy.

`Wire1` (17/16) and `Wire2` (25/24) also work — change `QWSTPAD_I2C_BUS` in
`config.h`.

### Address

Set by two cuttable traces on the back of the board. A0 is hardwired high,
which is why every address is odd.

| ADDR_SEL1 | ADDR_SEL2 | Address |
|---|---|---|
| intact | intact | **0x21** (default) |
| cut | intact | 0x23 |
| intact | cut | 0x25 |
| cut | cut | 0x27 |

The `INT` pin is not brought out to the connector, so the firmware polls
(every 4 ms by default — the I2C read costs about 115 µs at 400 kHz).

### Button bits

Read two bytes from register `0x00`; the result is `port1 << 8 | port0`. The
driver programs the TCA9555 polarity-inversion registers, so **1 = pressed**
even though the switches are physically active-low. Don't invert again.

| Bit | Pin | Button | Mask |
|---|---|---|---|
| 1 | P0_1 | Up | `0x0002` |
| 2 | P0_2 | Left | `0x0004` |
| 3 | P0_3 | Right | `0x0008` |
| 4 | P0_4 | Down | `0x0010` |
| 5 | P0_5 | `-` | `0x0020` |
| 11 | P1_3 | `+` | `0x0800` |
| 12 | P1_4 | B | `0x1000` |
| 13 | P1_5 | Y | `0x2000` |
| 14 | P1_6 | A | `0x4000` |
| 15 | P1_7 | X | `0x8000` |

Note the order: **Up, Left, Right, Down** — not the intuitive
Up/Down/Left/Right. Bits 6, 7, 9 and 10 are LED outputs and bits 0 and 8 are
unconnected package pins (P1_0 reads 1 permanently), so always mask with
`QP_ALL` (`0xF83E`) before testing.

The A/B/X/Y labels are worth 30 seconds of your time to confirm: Pimoroni's
schematic net names and their own driver disagree on which is which within each
pair. The driver's mapping is used here because it's what every shipped
Pimoroni example runs on. Build with debug enabled and press each button.

### LEDs

LEDs 1–4 are on bits 6, 7, 9, 10. They're wired anode-to-3V3 through 2.2 kΩ, so
the expander pin sinks current: **bit low = LED on**. `setLeds()` handles the
inversion. The firmware uses LED 1 for "USB enumerated" and LED 2 for drum
activity.

## Controller mapping

| Input | Sends |
|---|---|
| D-pad | hat switch, all 8 directions including diagonals |
| A | Cross — confirm |
| B | Circle — back |
| X | Square |
| Y | Triangle |
| `+` | Start |
| `-` | Select |
| `+` and `-` held ~0.6 s | PS / Home (Start and Select suppressed) |

Face buttons are sent without the pad/cymbal discriminator flags, so the game
treats them as gamepad presses rather than drum hits.

## Discrete switch alternative

Set `DPAD_MODE` to `DPAD_GPIO` in `config.h` and wire momentary switches from
the pins listed there to ground. Internal pull-ups are enabled; no external
resistors needed. You lose X and Y (there are only six pins assigned by
default) — add them if you want.

## Debug serial

USB serial doesn't exist when the board is built as USB Type: MPA — the USB
port is busy pretending to be a drum adapter. Debug output therefore goes to
**Serial1**, the hardware UART on pins 0 (RX) and 1 (TX). Connect a 3.3 V
USB-serial adapter (TX→pin 0, RX→pin 1, GND→GND) at 115200 baud.

Uncomment `DEBUG_ENABLED` in `config.h`. Add `DEBUG_MIDI_MONITOR` to print
every incoming MIDI message — that's how you profile a module whose note
assignments nobody has published: hit each pad and read off the numbers.

Alternatively build with **USB Type: MPA + Serial (debug)** and change
`DEBUG_PORT` to `Serial`.
