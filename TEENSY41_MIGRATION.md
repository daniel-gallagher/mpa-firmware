# Teensy 3.6 → Teensy 4.1 port

> This file replaces an earlier document of the same name that described the
> migration as a matter of adding `#if defined(__IMXRT1062__)` board detection
> to the sketch. That is not what the migration involves, and firmware written
> on that assumption cannot work: the MPA USB device type does not exist for
> Teensy 4 in any published core.

## The actual problem

The sketch calls `usb_mpa_reset_packet()`, `usb_mpa_set_button()`,
`usb_mpa_set_hat()` and `usb_mpa_send()`. None of those are library functions.
They live in a **modified Teensy core** — [`curiousjp/cores-mpa`](https://github.com/curiousjp/cores-mpa),
a fork of `PaulStoffregen/cores` that adds a USB device type whose descriptors
match a Mad Catz MIDI Pro Adapter.

That fork touches seven files, **all under `teensy3/`**:

```
README.md          |  11 +--
teensy3/WProgram.h |   1 +
teensy3/usb_desc.c | 124 +++++++++++++++-
teensy3/usb_desc.h |  26 ++++
teensy3/usb_mpa.c  | 103 ++++++++++++++  (new)
teensy3/usb_mpa.h  |  46 ++++++++       (new)
teensy3/yield.cpp  |   2 +
```

Nothing in `teensy4/`. Selecting a Teensy 4.1 in the IDE compiles `teensy4/`,
where `USB_MPA` is an unknown macro, `MPA_INTERFACE` is never defined, and the
whole feature compiles out. The board would enumerate as whatever stock USB
type you picked, or fail to link.

The fork is also pinned to a mid-2019 upstream (~600 commits behind) and its
Teensy 3.x driver is built on an API that has no Teensy 4 counterpart, so
merging it forward is not an option either. The port is a rewrite.

## Stack differences that mattered

| | Teensy 3.x (MK66, USBOTG) | Teensy 4.x (IMXRT1062) |
|---|---|---|
| Stack core | `teensy3/usb_dev.c` | `teensy4/usb.c` (there is no `usb_dev.c`) |
| Buffers | shared pool, `usb_mem.c`, `usb_malloc()` | per-driver static `transfer_t[]` + `DMAMEM` |
| Submit | `usb_tx(ep, packet)` | `usb_prepare_transfer()` + `usb_transmit()` |
| Backpressure | "is a packet free and <3 queued?" | "has `tx_transfer[head]` completed?" |
| Endpoint setup | table-driven from `ENDPOINTn_CONFIG` | each driver calls `usb_config_tx()` / `usb_config_rx()` |
| Data cache | none | **must** `arm_dcache_flush_delete()` before TX |
| Config descriptors | one | two (480 Mbit and 12 Mbit variants) |

### 1. Cache maintenance

The single most important difference. The IMXRT1062 has a data cache; the USB
controller DMAs straight out of physical memory. TX buffers must live in
`DMAMEM`, be 32-byte aligned, and be flushed before submission:

```c
memcpy(buffer, usb_mpa_data, MPA_PACK_SIZE);
usb_prepare_transfer(xfer, buffer, MPA_PACK_SIZE, 0);
arm_dcache_flush_delete(buffer, TX_BUFSIZE);   // no Teensy 3 equivalent
usb_transmit(MPA_TX_ENDPOINT, xfer);
```

Omit the flush and the controller sends whatever was in RAM before the CPU's
write-back landed. It usually still enumerates, which makes it a miserable bug
to chase.

### 2. The transmit timeout would not have compiled

The 3.x `usb_mpa_send()` waited on a busy count calibrated per `F_CPU`, from a
table of `#if F_CPU == ...` cases running 24 MHz through 256 MHz. A Teensy 4 at
600 MHz matches none of them, leaving `TX_TIMEOUT` undefined. The port uses
`systick_millis_count` deltas.

### 3. Endpoint 1 is off limits

Both `usb_config_tx()` and `usb_config_rx()` in `teensy4/usb.c` begin with
`if (ep < 2 || ep > NUM_ENDPOINTS) return;`, and the SET_CONFIGURATION handler
does this *after* every driver's configure call:

```c
#if defined(EXPERIMENTAL_INTERFACE)
memset(endpoint_queue_head + 2, 0, sizeof(endpoint_t) * 2);
endpoint_queue_head[2].pointer4 = 0xB8C6CF5D;
endpoint_queue_head[3].pointer4 = 0x74D59319;
#endif
```

Queue heads 2 and 3 are endpoint 1's. So the genuine adapter's EP1 IN / EP2 OUT
becomes **EP2 IN / EP3 OUT** here. Endpoint addresses are enumerated from the
descriptor, so no host notices.

### 4. Speed

Teensy 4 enumerates at high speed by default. The genuine adapter is a full
speed device, and at high speed `bInterval` is a 2^(n−1) *microframe* exponent —
`bInterval = 1` would mean 125 µs rather than 1 ms. `MPA_FORCE_FULL_SPEED` sets
`USB1_PORTSC1 |= USB_PORTSC1_PFSC` so the descriptor's timing means what it
says and the device presents the way the console expects. Comment it out in
the `USB_MPA` block of `usb_desc.h` to try high speed.

The MPA interface block is added to **both** `usb_config_descriptor_480[]` and
`usb_config_descriptor_12[]`; for this device they come out identical, which
conveniently means the speed choice can't produce a descriptor mismatch.

### 5. Two bugs inherited from the 3.x fork, fixed

**`MPA_DESC_OFFSET` was wrong.** The fork defined it as `9`, but the class HID
descriptor sits at byte **18** of the config descriptor (9-byte config header +
9-byte interface descriptor). A host issuing `GET_DESCRIPTOR(0x21)` got the
interface descriptor back. It never bit anyone because the PS3 fetches the
report descriptor (`0x2200`) directly and never asks for `0x2100`. Rather than
hardcode 18, the port extends the core's own position-macro chain:

```c
#define MPA_INTERFACE_DESC_POS   EXPERIMENTAL_INTERFACE_DESC_POS+EXPERIMENTAL_INTERFACE_DESC_SIZE
#define MPA_INTERFACE_DESC_SIZE  9+9+7+7
#define MPA_HID_DESC_OFFSET      MPA_INTERFACE_DESC_POS+9
#define CONFIG_DESC_SIZE         MPA_INTERFACE_DESC_POS+MPA_INTERFACE_DESC_SIZE
```

so the offset is correct by construction and stays correct in the two-interface
debug variant, where it evaluates to 50 instead of 18.

**`iSerialNumber` was clobbered globally.** The fork changed the device
descriptor's `iSerialNumber` from 3 to 0 *unconditionally*, not under
`#ifdef MPA_INTERFACE`. Any other USB type built from that core lost its serial
number string, which is what Teensy's port identification relies on. Here it's
conditional.

### 6. The `Serial` linkage trap

`USB_MPA` defines neither `CDC_DATA_INTERFACE` nor `SEREMU_INTERFACE`, so
`usb_inst.cpp` never instantiates a `Serial` object — but `yield.cpp`
references `Serial.available()` unconditionally and fails to link. Same trap on
both cores. The installer guards that call.

The cleaner alternative is to include a SEREMU interface, which also restores
Teensy Loader auto-reboot and the Serial Monitor. That's what the
`USB_MPA_SEREMU` build type does, but it adds a second USB interface the real
adapter doesn't have — fine on a PC, not what you want facing a console.

## Verification

The port is compile-verified with `arm-none-eabi-gcc` for cortex-m7 in both
configurations, and the emitted descriptors were extracted from the object file
and parsed:

```
usb_config_descriptor_480  len=41
09 02 29 00 01 01 00 80 20  09 04 00 00 02 03 00 00 00
09 21 11 01 00 01 22 89 00  07 05 82 03 40 00 01  07 05 03 03 40 00 01

  @  0  len=9  CONFIG     wTotalLength=41 (= array length)  bNumInterfaces=1
                          bmAttributes=0x80  bMaxPower=32 (64 mA)
  @  9  len=9  INTERFACE  #0, 2 endpoints, class 0x03 (HID)
  @ 18  len=9  HID        wDescriptorLength=137 (= sizeof mpa_report_desc)
  @ 27  len=7  ENDPOINT   0x82 IN,  interrupt, 64 bytes, bInterval 1
  @ 34  len=7  ENDPOINT   0x03 OUT, interrupt, 64 bytes, bInterval 1

device_descriptor: bcdUSB 0x0200, bMaxPacketSize0 16, VID 0x12BA,
                   PID 0x0218, bcdDevice 0x0200, iSerialNumber 0

usb_descriptor_list: {0x2100, iface 0, config_480 + 18, 9}   <- offset correct
                     {0x0301, 0x0048, manufacturer_name, 0}  <- PS3 handshake
```

`usb_config_descriptor_12[]` is byte-identical. The debug variant produces 73
bytes with interfaces in ascending order (SEREMU 0, MPA 1).

`tools/install_mpa_core.py --uninstall` was confirmed to restore every patched
core file byte-for-byte identical to a fresh `git clone` of upstream.
