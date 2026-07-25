#!/usr/bin/env python3
"""
Install the MPA (MIDI Pro Adapter) USB device type into a Teensyduino
Teensy 4.x core.

The Teensy 3.x original (curiousjp/cores-mpa) only ever patched teensy3/.
Teensy 4.x uses a completely different USB device stack, so this installs a
fresh implementation rather than merging that 2019 fork forward.

Every edit is anchored on an exact string from the stock core and is
idempotent: re-running the script is a no-op, and --uninstall reverses it.
Originals are backed up to <file>.mpa-orig on first run.

Usage:
    python3 install_mpa_core.py --core-dir /path/to/hardware/teensy/avr/cores/teensy4
    python3 install_mpa_core.py --core-dir ... --boards-dir /path/to/hardware/teensy/avr
    python3 install_mpa_core.py --core-dir ... --uninstall
    python3 install_mpa_core.py --core-dir ... --check

If --core-dir is omitted the script tries the usual Teensyduino locations.
"""

import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATCH_SRC = os.path.join(HERE, "..", "core-patch", "teensy4")

MARK = "MPA_INTERFACE"  # presence of this in a file means we already patched it

# Common Teensyduino core locations, newest layout first.
CANDIDATE_CORES = [
    "~/Library/Arduino15/packages/teensy/hardware/avr/*/cores/teensy4",
    "~/.arduino15/packages/teensy/hardware/avr/*/cores/teensy4",
    "/Applications/Arduino.app/Contents/Java/hardware/teensy/avr/cores/teensy4",
    "~/Documents/Arduino/hardware/teensy/avr/cores/teensy4",
    "C:/Program Files (x86)/Arduino/hardware/teensy/avr/cores/teensy4",
]


# ---------------------------------------------------------------------------
# The USB type configuration blocks, inserted into usb_desc.h
# ---------------------------------------------------------------------------
# Endpoint choice note: the genuine Mad Catz adapter uses EP1 IN and EP2 OUT.
# On Teensy 4 endpoint 1 is unusable from the driver API - usb_config_tx() and
# usb_config_rx() both bail out on "ep < 2", and usb.c scribbles magic values
# over endpoint 1's queue heads after SET_CONFIGURATION. So we use EP2 IN and
# EP3 OUT. Hosts enumerate endpoint addresses from the descriptor, so this is
# invisible to the console.

USB_DESC_H_BLOCK = r"""
#elif defined(USB_MPA) || defined(USB_MPA_SEREMU)
  // Mad Catz / Harmonix "MIDI Pro Adapter" drum controller emulation.
  // Identity below is copied from a genuine PS3 adapter.
  #define VENDOR_ID		0x12BA
  #define PRODUCT_ID		0x0218
  #define MANUFACTURER_NAME	{'L','i','c','e','n','s','e','d',' ','b','y',' ','S','o','n','y',' ','C','o','m','p','u','t','e','r',' ','E','n','t','e','r','t','a','i','n','m','e','n','t',' ','A','m','e','r','i','c','a'}
  #define MANUFACTURER_NAME_LEN	47
  #define PRODUCT_NAME		{'H','a','r','m','o','n','i','x',' ','D','r','u','m',' ','k','i','t',' ','f','o','r',' ','P','l','a','y','S','t','a','t','i','o','n',0xae,'3'}
  #define PRODUCT_NAME_LEN	35
  #define DEVICE_CLASS		0x00
  #define DEVICE_SUBCLASS	0x00
  #define DEVICE_PROTOCOL	0x00
  #define BCD_DEVICE		0x0200
  // The genuine adapter reports a 16 byte control endpoint. Set this to 64 if
  // you would rather use the Teensy default; MPA_EP0_SIZE_OVERRIDE below makes
  // usb.c honour whatever is set here.
  #define EP0_SIZE		16
  #define MPA_EP0_SIZE_OVERRIDE	1
  // The genuine adapter is a full speed (12 Mbit) device. Teensy 4 defaults to
  // high speed, which changes bInterval from milliseconds to 125us microframes
  // and makes the device look nothing like the real one. Comment out to try
  // high speed.
  #define MPA_FORCE_FULL_SPEED	1
  #define MPA_TX_ENDPOINT	2
  #define MPA_RX_ENDPOINT	3
  #define MPA_PACK_SIZE		0x1B	// 27 byte input report
  #define MPA_MAX_SIZE		0x40
  #define MPA_INTERVAL		1	// ms; the genuine adapter uses 10
  #define ENDPOINT2_CONFIG	ENDPOINT_RECEIVE_UNUSED + ENDPOINT_TRANSMIT_INTERRUPT
  #define ENDPOINT3_CONFIG	ENDPOINT_RECEIVE_INTERRUPT + ENDPOINT_TRANSMIT_UNUSED
  #ifdef USB_MPA_SEREMU
    // Development variant: adds an emulated serial port so Serial works, the
    // Serial Monitor works, and Teensy Loader can auto-reboot the board.
    // This adds a second interface, which the genuine adapter does not have -
    // use plain USB_MPA on the console.
    //
    // Interface numbers follow the order the blocks appear in the config
    // descriptor: SEREMU's block is emitted first by the core's position
    // chain, so it takes interface 0 and MPA takes interface 1. Getting this
    // backwards produces a descriptor whose interfaces are not in ascending
    // order, which some hosts reject.
    #define NUM_ENDPOINTS	4
    #define NUM_INTERFACE	2
    #define SEREMU_INTERFACE	0
    #define MPA_INTERFACE	1	// Midi Pro Adapter
    #define SEREMU_TX_ENDPOINT	4
    #define SEREMU_TX_SIZE	64
    #define SEREMU_TX_INTERVAL	1
    #define SEREMU_RX_ENDPOINT	4
    #define SEREMU_RX_SIZE	32
    #define SEREMU_RX_INTERVAL	2
    #define ENDPOINT4_CONFIG	ENDPOINT_RECEIVE_INTERRUPT + ENDPOINT_TRANSMIT_INTERRUPT
  #else
    #define NUM_ENDPOINTS	3
    #define NUM_INTERFACE	1
    #define MPA_INTERFACE	0	// Midi Pro Adapter
  #endif
"""

# ---------------------------------------------------------------------------
# usb_desc.c pieces
# ---------------------------------------------------------------------------

MPA_REPORT_DESC = r"""
#ifdef MPA_INTERFACE
// HID report descriptor for the Mad Catz MIDI Pro Adapter, captured from a
// genuine PS3 unit. 13 buttons + 3 constant pad bits, an 8 direction hat with
// a null state, four 8 bit axes, twelve vendor bytes (per pad velocity), an
// 8 byte feature report, an 8 byte output report, and four 16 bit vendor
// fields. Total input report length is 27 bytes (MPA_PACK_SIZE).
static uint8_t mpa_report_desc[] = {
	0x05, 0x01,		// Usage Page (Generic Desktop Ctrls)
	0x09, 0x05,		// Usage (Game Pad)
	0xA1, 0x01,		// Collection (Application)
	0x15, 0x00,		//   Logical Minimum (0)
	0x25, 0x01,		//   Logical Maximum (1)
	0x35, 0x00,		//   Physical Minimum (0)
	0x45, 0x01,		//   Physical Maximum (1)
	0x75, 0x01,		//   Report Size (1)
	0x95, 0x0D,		//   Report Count (13)
	0x05, 0x09,		//   Usage Page (Button)
	0x19, 0x01,		//   Usage Minimum (0x01)
	0x29, 0x0D,		//   Usage Maximum (0x0D)
	0x81, 0x02,		//   Input (Data,Var,Abs)
	0x95, 0x03,		//   Report Count (3)
	0x81, 0x01,		//   Input (Const,Array,Abs)
	0x05, 0x01,		//   Usage Page (Generic Desktop Ctrls)
	0x25, 0x07,		//   Logical Maximum (7)
	0x46, 0x3B, 0x01,	//   Physical Maximum (315)
	0x75, 0x04,		//   Report Size (4)
	0x95, 0x01,		//   Report Count (1)
	0x65, 0x14,		//   Unit (English Rotation, degrees)
	0x09, 0x39,		//   Usage (Hat switch)
	0x81, 0x42,		//   Input (Data,Var,Abs,Null State)
	0x65, 0x00,		//   Unit (None)
	0x95, 0x01,		//   Report Count (1)
	0x81, 0x01,		//   Input (Const,Array,Abs)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x46, 0xFF, 0x00,	//   Physical Maximum (255)
	0x09, 0x30,		//   Usage (X)
	0x09, 0x31,		//   Usage (Y)
	0x09, 0x32,		//   Usage (Z)
	0x09, 0x35,		//   Usage (Rz)
	0x75, 0x08,		//   Report Size (8)
	0x95, 0x04,		//   Report Count (4)
	0x81, 0x02,		//   Input (Data,Var,Abs)
	0x06, 0x00, 0xFF,	//   Usage Page (Vendor Defined 0xFF00)
	0x09, 0x20, 0x09, 0x21, 0x09, 0x22, 0x09, 0x23,
	0x09, 0x24, 0x09, 0x25, 0x09, 0x26, 0x09, 0x27,
	0x09, 0x28, 0x09, 0x29, 0x09, 0x2A, 0x09, 0x2B,
	0x95, 0x0C,		//   Report Count (12)
	0x81, 0x02,		//   Input (Data,Var,Abs)
	0x0A, 0x21, 0x26,	//   Usage (0x2621)
	0x95, 0x08,		//   Report Count (8)
	0xB1, 0x02,		//   Feature (Data,Var,Abs)
	0x0A, 0x21, 0x26,	//   Usage (0x2621)
	0x91, 0x02,		//   Output (Data,Var,Abs)
	0x26, 0xFF, 0x03,	//   Logical Maximum (1023)
	0x46, 0xFF, 0x03,	//   Physical Maximum (1023)
	0x09, 0x2C, 0x09, 0x2D, 0x09, 0x2E, 0x09, 0x2F,
	0x75, 0x10,		//   Report Size (16)
	0x95, 0x04,		//   Report Count (4)
	0x81, 0x02,		//   Input (Data,Var,Abs)
	0xC0			// End Collection
};
#endif // MPA_INTERFACE

"""

# The 3.x fork hand-computed CONFIG_DESC_SIZE and defined MPA_DESC_OFFSET as 9,
# which pointed at the interface descriptor instead of the HID descriptor. We
# extend the core's own position chain instead, so the offset is correct by
# construction.
MPA_DESC_CHAIN = r"""
#define MPA_INTERFACE_DESC_POS		EXPERIMENTAL_INTERFACE_DESC_POS+EXPERIMENTAL_INTERFACE_DESC_SIZE
#ifdef  MPA_INTERFACE
#define MPA_INTERFACE_DESC_SIZE		9+9+7+7
#define MPA_HID_DESC_OFFSET		MPA_INTERFACE_DESC_POS+9
#else
#define MPA_INTERFACE_DESC_SIZE		0
#endif

#define CONFIG_DESC_SIZE		MPA_INTERFACE_DESC_POS+MPA_INTERFACE_DESC_SIZE
"""

MPA_IFACE_BLOCK = r"""
#ifdef MPA_INTERFACE
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        MPA_INTERFACE,                          // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x03,                                   // bInterfaceClass (0x03 = HID)
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // HID interface descriptor, HID 1.11 spec, section 6.2.1
        9,                                      // bLength
        0x21,                                   // bDescriptorType
        0x11, 0x01,                             // bcdHID
        0,                                      // bCountryCode
        1,                                      // bNumDescriptors
        0x22,                                   // bDescriptorType
        LSB(sizeof(mpa_report_desc)),           // wDescriptorLength
        MSB(sizeof(mpa_report_desc)),
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        MPA_TX_ENDPOINT | 0x80,                 // bEndpointAddress (IN)
        0x03,                                   // bmAttributes (0x03=intr)
        MPA_MAX_SIZE, 0,                        // wMaxPacketSize
        MPA_INTERVAL,                           // bInterval
        7,                                      // bLength
        5,                                      // bDescriptorType
        MPA_RX_ENDPOINT,                        // bEndpointAddress (OUT)
        0x03,                                   // bmAttributes (0x03=intr)
        MPA_MAX_SIZE, 0,                        // wMaxPacketSize
        MPA_INTERVAL,                           // bInterval
#endif // MPA_INTERFACE
};"""

MPA_DESC_LIST = r"""#ifdef MPA_INTERFACE
        {0x2200, MPA_INTERFACE, mpa_report_desc, sizeof(mpa_report_desc)},
        {0x2100, MPA_INTERFACE, usb_config_descriptor_480+MPA_HID_DESC_OFFSET, 9},
        // The PS3 asks for string descriptor 1 with a bogus language ID of
        // 0x0048 during its handshake. Without an entry that matches, usb.c
        // stalls endpoint 0 and enumeration fails.
        {0x0301, 0x0048, (const uint8_t *)&usb_string_manufacturer_name, 0},
#endif
"""


# ---------------------------------------------------------------------------
# Edit table: (filename, anchor, replacement, description)
# Each replacement must contain the anchor so the edit is a pure insertion.
# ---------------------------------------------------------------------------

def build_edits():
    return [
        # ---- usb_desc.h -------------------------------------------------
        (
            "usb_desc.h",
            "#elif defined(USB_MIDI16_AUDIO_SERIAL)",
            USB_DESC_H_BLOCK.lstrip("\n") + "\n#elif defined(USB_MIDI16_AUDIO_SERIAL)",
            "USB_MPA / USB_MPA_SEREMU configuration block",
        ),
        # ---- usb_desc.c: report descriptor ------------------------------
        (
            "usb_desc.c",
            "#define CONFIG_HEADER_DESCRIPTOR_SIZE\t9",
            MPA_REPORT_DESC.lstrip("\n") + "#define CONFIG_HEADER_DESCRIPTOR_SIZE\t9",
            "MPA HID report descriptor",
        ),
        # ---- usb_desc.c: descriptor position chain ----------------------
        (
            "usb_desc.c",
            "#define CONFIG_DESC_SIZE\t\tEXPERIMENTAL_INTERFACE_DESC_POS+EXPERIMENTAL_INTERFACE_DESC_SIZE",
            MPA_DESC_CHAIN.strip("\n"),
            "MPA position in the descriptor chain (fixes the 3.x MPA_DESC_OFFSET bug)",
        ),
        # ---- usb_desc.c: interface block in BOTH config arrays ----------
        (
            "usb_desc.c",
            "#endif // EXPERIMENTAL_INTERFACE\n};",
            "#endif // EXPERIMENTAL_INTERFACE\n" + MPA_IFACE_BLOCK.lstrip("\n"),
            "MPA interface + endpoint descriptors (both speed variants)",
            2,  # expect exactly two occurrences
        ),
        # ---- usb_desc.c: bus-powered attributes -------------------------
        (
            "usb_desc.c",
            "        0xC0,                                   // bmAttributes\n"
            "        50,                                     // bMaxPower",
            "#ifdef MPA_INTERFACE\n"
            "        0x80,                                   // bmAttributes (bus powered, matches the genuine adapter)\n"
            "        0x20,                                   // bMaxPower (32*2 = 64 mA)\n"
            "#else\n"
            "        0xC0,                                   // bmAttributes\n"
            "        50,                                     // bMaxPower\n"
            "#endif",
            "power descriptor fields matching the genuine adapter",
            2,
        ),
        # ---- usb_desc.c: iSerialNumber ----------------------------------
        (
            "usb_desc.c",
            "        3,                                      // iSerialNumber",
            "#ifdef MPA_INTERFACE\n"
            "        0,                                      // iSerialNumber (the genuine adapter has none)\n"
            "#else\n"
            "        3,                                      // iSerialNumber\n"
            "#endif",
            "iSerialNumber (conditional - the 3.x fork changed this globally)",
        ),
        # ---- usb_desc.c: descriptor list --------------------------------
        (
            "usb_desc.c",
            "#ifdef MTP_INTERFACE\n\t{0x0304, 0x0409, (const uint8_t *)&usb_string_mtp, 0},",
            MPA_DESC_LIST + "#ifdef MTP_INTERFACE\n\t{0x0304, 0x0409, (const uint8_t *)&usb_string_mtp, 0},",
            "MPA descriptor list entries incl. the PS3 langid 0x0048 string alias",
        ),
        # ---- usb.c: include ---------------------------------------------
        (
            "usb.c",
            '#include "usb_joystick.h"',
            '#include "usb_joystick.h"\n#include "usb_mpa.h"',
            "usb_mpa.h include in usb.c",
        ),
        # ---- usb.c: configure hook --------------------------------------
        (
            "usb.c",
            "\t\t#if defined(JOYSTICK_INTERFACE)\n\t\tusb_joystick_configure();\n\t\t#endif",
            "\t\t#if defined(JOYSTICK_INTERFACE)\n\t\tusb_joystick_configure();\n\t\t#endif\n"
            "\t\t#if defined(MPA_INTERFACE)\n\t\tusb_mpa_configure();\n\t\t#endif",
            "usb_mpa_configure() call on SET_CONFIGURATION",
        ),
        # ---- usb.c: control endpoint size -------------------------------
        (
            "usb.c",
            "\tendpoint_queue_head[0].config = (64 << 16) | (1 << 15);\n"
            "\tendpoint_queue_head[1].config = (64 << 16);",
            "#if defined(MPA_EP0_SIZE_OVERRIDE) && defined(EP0_SIZE)\n"
            "\t// The MPA type declares a 16 byte control endpoint like the real\n"
            "\t// adapter; the hardware queue head has to agree with the descriptor.\n"
            "\tendpoint_queue_head[0].config = (EP0_SIZE << 16) | (1 << 15);\n"
            "\tendpoint_queue_head[1].config = (EP0_SIZE << 16);\n"
            "#else\n"
            "\tendpoint_queue_head[0].config = (64 << 16) | (1 << 15);\n"
            "\tendpoint_queue_head[1].config = (64 << 16);\n"
            "#endif",
            "honour EP0_SIZE for the MPA type",
        ),
        # ---- usb.c: force full speed ------------------------------------
        (
            "usb.c",
            "\t//USB1_PORTSC1 |= USB_PORTSC1_PFSC; // force 12 Mbit/sec",
            "\t//USB1_PORTSC1 |= USB_PORTSC1_PFSC; // force 12 Mbit/sec\n"
            "#if defined(MPA_FORCE_FULL_SPEED)\n"
            "\t// The genuine MIDI Pro Adapter is a full speed device. Running at\n"
            "\t// high speed would reinterpret bInterval as 125us microframes and\n"
            "\t// make the device look nothing like the one the console expects.\n"
            "\tUSB1_PORTSC1 |= USB_PORTSC1_PFSC;\n"
            "#endif",
            "force full speed for the MPA type",
        ),
        # ---- WProgram.h -------------------------------------------------
        (
            "WProgram.h",
            '#include "usb_rawhid.h"',
            '#include "usb_rawhid.h"\n#include "usb_mpa.h"',
            "usb_mpa.h include so sketches get the API automatically",
        ),
        # ---- yield.cpp --------------------------------------------------
        # Plain USB_MPA defines neither CDC nor SEREMU, so usb_inst.cpp never
        # creates a Serial object and yield() fails to link.
        (
            "yield.cpp",
            "\tif (check_flags & YIELD_CHECK_USB_SERIAL) {\n\t\tif (Serial.available()) serialEvent();\n\t}",
            "\tif (check_flags & YIELD_CHECK_USB_SERIAL) {\n"
            "#if !defined(MPA_INTERFACE) || defined(SEREMU_INTERFACE) || defined(CDC_DATA_INTERFACE)\n"
            "\t\tif (Serial.available()) serialEvent();\n"
            "#endif\n\t}",
            "guard the USB Serial hook (no Serial object exists for plain USB_MPA)",
        ),
    ]


BOARDS_LOCAL = """# Adds a "USB Type: MPA" entry to the Tools menu for Teensy 4.0 and 4.1.
#
# Arduino reads boards.local.txt in addition to boards.txt, so this survives a
# Teensyduino update without you having to re-edit boards.txt.
#
# fake_serial=teensy_gateway tells the IDE not to expect a real serial port for
# this USB type.

teensy41.menu.usb.mpa=MPA (Rock Band drum adapter)
teensy41.menu.usb.mpa.build.usbtype=USB_MPA
teensy41.menu.usb.mpa.fake_serial=teensy_gateway
teensy41.menu.usb.mpadbg=MPA + Serial (debug)
teensy41.menu.usb.mpadbg.build.usbtype=USB_MPA_SEREMU

teensy40.menu.usb.mpa=MPA (Rock Band drum adapter)
teensy40.menu.usb.mpa.build.usbtype=USB_MPA
teensy40.menu.usb.mpa.fake_serial=teensy_gateway
teensy40.menu.usb.mpadbg=MPA + Serial (debug)
teensy40.menu.usb.mpadbg.build.usbtype=USB_MPA_SEREMU
"""


def find_core_dir():
    import glob
    for pat in CANDIDATE_CORES:
        for hit in sorted(glob.glob(os.path.expanduser(pat)), reverse=True):
            if os.path.isfile(os.path.join(hit, "usb_desc.h")):
                return hit
    return None


def backup(path):
    orig = path + ".mpa-orig"
    if not os.path.exists(orig):
        shutil.copy2(path, orig)


def apply_edits(core, uninstall=False, check=False):
    edits = build_edits()
    problems = []
    changes = []

    # Group edits per file so we write once.
    per_file = {}
    for e in edits:
        per_file.setdefault(e[0], []).append(e)

    for fname, file_edits in per_file.items():
        path = os.path.join(core, fname)
        if not os.path.isfile(path):
            problems.append(f"missing file: {path}")
            continue
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        original = text

        for e in file_edits:
            _, anchor, replacement, desc = e[:4]
            expect = e[4] if len(e) > 4 else 1

            if uninstall:
                if replacement in text:
                    text = text.replace(replacement, anchor)
                    changes.append(f"  reverted: {fname} - {desc}")
                continue

            if replacement in text:
                changes.append(f"  already applied: {fname} - {desc}")
                continue
            n = text.count(anchor)
            if n != expect:
                problems.append(
                    f"{fname}: anchor for '{desc}' found {n} time(s), expected {expect}.\n"
                    f"    anchor: {anchor.splitlines()[0][:70]!r}"
                )
                continue
            text = text.replace(anchor, replacement)
            changes.append(f"  patched: {fname} - {desc}")

        if text != original and not check:
            backup(path)
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                f.write(text)

    # usb_mpa.c / usb_mpa.h
    for fn in ("usb_mpa.h", "usb_mpa.c"):
        dst = os.path.join(core, fn)
        src = os.path.join(PATCH_SRC, fn)
        if uninstall:
            if os.path.exists(dst):
                if not check:
                    os.remove(dst)
                changes.append(f"  removed: {fn}")
        else:
            if not os.path.isfile(src):
                problems.append(f"missing source file: {src}")
                continue
            same = os.path.exists(dst) and open(dst, "rb").read() == open(src, "rb").read()
            if same:
                changes.append(f"  already installed: {fn}")
            else:
                if not check:
                    shutil.copy2(src, dst)
                changes.append(f"  installed: {fn}")

    return changes, problems


def install_boards(boards_dir, uninstall=False, check=False):
    path = os.path.join(boards_dir, "boards.local.txt")
    if uninstall:
        if os.path.exists(path) and "usb.mpa" in open(path).read():
            if not check:
                os.remove(path)
            return [f"  removed: {path}"], []
        return [], []
    existing = open(path).read() if os.path.exists(path) else ""
    if "teensy41.menu.usb.mpa=" in existing:
        return [f"  already present: {path}"], []
    if not check:
        with open(path, "a") as f:
            if existing and not existing.endswith("\n"):
                f.write("\n")
            f.write(BOARDS_LOCAL)
    return [f"  wrote: {path}"], []


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core-dir", help="path to hardware/teensy/avr/cores/teensy4")
    ap.add_argument("--boards-dir", help="path to hardware/teensy/avr (for boards.local.txt)")
    ap.add_argument("--uninstall", action="store_true", help="revert all changes")
    ap.add_argument("--check", action="store_true", help="report what would change, write nothing")
    args = ap.parse_args()

    core = args.core_dir or find_core_dir()
    if not core or not os.path.isfile(os.path.join(core, "usb_desc.h")):
        print("Could not locate a Teensy 4 core directory.", file=sys.stderr)
        print("Pass --core-dir /path/to/hardware/teensy/avr/cores/teensy4", file=sys.stderr)
        return 2
    core = os.path.abspath(core)
    print(f"Teensy 4 core: {core}")

    changes, problems = apply_edits(core, args.uninstall, args.check)

    boards_dir = args.boards_dir
    if not boards_dir:
        guess = os.path.abspath(os.path.join(core, "..", ".."))
        if os.path.isfile(os.path.join(guess, "boards.txt")):
            boards_dir = guess
    if boards_dir:
        c, p = install_boards(boards_dir, args.uninstall, args.check)
        changes += c
        problems += p
    else:
        problems.append("could not locate boards.txt; pass --boards-dir to add the menu entry")

    for line in changes:
        print(line)
    if problems:
        print("\nPROBLEMS:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    verb = "would be reverted" if (args.uninstall and args.check) else \
           "reverted" if args.uninstall else \
           "would be applied" if args.check else "applied"
    print(f"\nOK - all changes {verb}.")
    if not args.uninstall and not args.check:
        print('Restart the Arduino IDE, then pick Tools > USB Type > "MPA (Rock Band drum adapter)".')
    return 0


if __name__ == "__main__":
    sys.exit(main())
