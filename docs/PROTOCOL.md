# The MPA USB protocol

Reference for the descriptors and report format the firmware emits. Everything
here comes from curiousjp's capture of a genuine Mad Catz MIDI Pro Adapter,
re-verified against the bytes this core actually produces.

## Identity

| Field | Value |
|---|---|
| idVendor | `0x12BA` — "Licensed by Sony Computer Entertainment America" |
| idProduct | `0x0218` — "Harmonix Drum kit for PlayStation®3" |
| bcdUSB / bcdDevice | `0x0200` |
| bMaxPacketSize0 | 16 (Teensy's default is 64; the real adapter uses 16) |
| iSerialNumber | 0 — the real adapter has no serial string |
| bmAttributes / bMaxPower | `0x80` / `0x20` — bus powered, 64 mA |
| Speed | full speed (12 Mbit) |
| Interfaces | 1 (HID) |
| Endpoints | EP2 IN interrupt, EP3 OUT interrupt, 64 bytes, `bInterval` 1 ms |

The genuine adapter uses EP1 IN / EP2 OUT and `bInterval` 10. See
[TEENSY41_MIGRATION.md](../TEENSY41_MIGRATION.md#3-endpoint-1-is-off-limits)
for why the endpoint numbers differ, and `MPA_INTERVAL` in the `USB_MPA` block
if you want to match the 10 ms polling too.

## The 27-byte input report

| Offset | Size | Contents |
|---|---|---|
| 0 | 1 | buttons 0–7 bitmask |
| 1 | 1 | buttons 8–12 in bits 0–4; bits 5–7 are declared constant padding |
| 2 | 1 | hat switch in the low nibble (0–7, 8 = null); high nibble padding |
| 3–6 | 4 | X, Y, Z, Rz — 8-bit, idle `0x80` |
| 7–18 | 12 | vendor usages `0x20`–`0x2B` — per-pad velocity on a real kit |
| 19–26 | 8 | vendor usages `0x2C`–`0x2F`, four 16-bit LE fields, idle `0x0200` |

`usb_mpa_reset_packet()` clears bytes 0–2 and 7–18. Everything else is a
constant lifted from the real device and is never touched.

The descriptor also declares an 8-byte Feature report and an 8-byte Output
report (both `Usage 0x2621`, the classic PS3 vendor usage). We don't act on
either, but unlike the Teensy 3.x version we do keep a receive transfer armed
on the OUT endpoint so it doesn't NAK forever.

## Buttons

| Bit | PS3 name | Rock Band meaning |
|---|---|---|
| 0 | Square | blue pad / blue cymbal |
| 1 | Cross | green pad / green cymbal |
| 2 | Circle | red pad |
| 3 | Triangle | yellow pad / yellow cymbal |
| 4 | L1 | kick pedal |
| 5 | R1 | second kick pedal |
| 6 | L2 | — |
| 7 | R2 | — |
| 8 | Select | Select / Back |
| 9 | Start | Start / Options |
| 10 | — | **pad flag**: this press was a drum pad |
| 11 | — | **cymbal flag**: this press was a cymbal |
| 12 | — | PS / Home |

## The hat is overloaded — this is the important part

There are seven drum zones but only four coloured face buttons. The protocol
resolves this with the two discriminator flags plus the hat:

| Zone | Face button | Flag 10 | Flag 11 | Hat |
|---|---|---|---|---|
| red pad | Circle | ✓ | | neutral |
| yellow pad | Triangle | ✓ | | neutral |
| blue pad | Square | ✓ | | neutral |
| green pad | Cross | ✓ | | neutral |
| yellow cymbal | Triangle | | ✓ | **up** |
| blue cymbal | Square | | ✓ | **down** |
| green cymbal | Cross | | ✓ | neutral |
| kick | L1 | | | neutral |

So the hat is doing double duty: it's the D-pad, and it's the yellow/blue
cymbal selector. Two consequences the firmware has to respect:

1. **While a cymbal is sounding, the hat belongs to the cymbal.** If a held
   D-pad direction overwrites it, the game reads a different cymbal than the
   one you hit. `teensympa.ino` gives cymbals priority; a D-pad direction is
   simply deferred for the ~25 ms the cymbal is held.

2. **A face button with neither flag set is a plain gamepad press.** The drums
   always set flag 10 or 11, so they can never produce one. That's what makes
   the D-pad's A/B/X/Y unambiguous in menus.

## If the console rejects the device

Roughly in order of likelihood:

1. **The `0x0048` string alias.** During its handshake the PS3 issues
   `GET_DESCRIPTOR(String 1)` with a bogus language ID of `0x0048`. Without a
   matching entry in `usb_descriptor_list[]`, `usb.c` stalls endpoint 0 and
   enumeration dies. The installer adds it; confirm it survived by grepping
   `0x0301, 0x0048` in your patched `teensy4/usb_desc.c`.

2. **Speed.** Try commenting out `MPA_FORCE_FULL_SPEED` in the `USB_MPA` block,
   or conversely confirm it's still defined — a Teensy 4 defaults to high speed
   and the real adapter is full speed.

3. **Polling interval.** Set `MPA_INTERVAL` to `10` to match the genuine
   adapter exactly. Costs you a little latency.

4. **`EP0_SIZE`.** Set to 16 here to match the real device. If enumeration is
   flaky, try 64 (the Teensy default) — `MPA_EP0_SIZE_OVERRIDE` keeps the
   hardware queue head in step with whatever you choose.

5. **Endpoint numbers.** If you suspect the console cares, moving to EP1 means
   patching `usb_config_tx()`/`usb_config_rx()` to allow `ep == 1` *and*
   guarding the `EXPERIMENTAL_INTERFACE` memset in `usb.c` that stomps EP1's
   queue heads. Try everything else first.

6. **Authentication.** A PS4/PS5 title that demands a signed authentication
   response cannot be satisfied by any of this — the security chip in a
   licensed adapter is doing work we can't reproduce. The PS3-era descriptor
   set works because Rock Band 4 accepts legacy PS3 instruments; if a future
   patch tightens that, no descriptor tweak will help.

Build with **USB Type: MPA + Serial (debug)**, plug into a PC, and check the
descriptors with `lsusb -v` (Linux), `USB Prober` (macOS) or USB Tree View
(Windows) before you take it to the console. Anything wrong in the descriptor
will show up there.
