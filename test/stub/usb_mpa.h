/* Teensy 4.x MPA (MIDI Pro Adapter) USB device type
 *
 * Emulates the Mad Catz / Harmonix "Drum kit for PlayStation(R)3" USB
 * descriptor set (VID 0x12BA, PID 0x0218) so a Teensy 4.0/4.1 can act as a
 * Rock Band drum controller.
 *
 * Original Teensy 3.x implementation by curiousjp (github.com/curiousjp/cores-mpa).
 * Teensy 4.x (IMXRT1062) port: the 3.x version used the usb_malloc()/usb_tx()
 * packet-pool API from teensy3/usb_mem.c, which does not exist on Teensy 4.
 * This version uses the transfer-descriptor API in teensy4/usb.c
 * (usb_config_tx / usb_prepare_transfer / usb_transmit) and adds the cache
 * maintenance that the IMXRT1062's data cache requires.
 *
 * Licensed under the same terms as the Teensyduino core library.
 */

#ifndef USBmpa_h_
#define USBmpa_h_

#include "usb_desc.h"

#if defined(MPA_INTERFACE)

#include <inttypes.h>

// ---------------------------------------------------------------------------
// Hat switch (D-pad) positions.
//
// The HID report descriptor declares the hat as a 4-bit field with logical
// max 7 and a null state, so 0..7 are the eight compass directions and 8 means
// "centred". The Teensy 3.x version only named four of these; all nine are
// named here.
// ---------------------------------------------------------------------------
#define MPA_HAT_UP              0x00
#define MPA_HAT_UP_RIGHT        0x01
#define MPA_HAT_RIGHT           0x02
#define MPA_HAT_DOWN_RIGHT      0x03
#define MPA_HAT_DOWN            0x04
#define MPA_HAT_DOWN_LEFT       0x05
#define MPA_HAT_LEFT            0x06
#define MPA_HAT_UP_LEFT         0x07
#define MPA_HAT_NEUTRAL         0x08

// ---------------------------------------------------------------------------
// Button indices.
//
// These follow the PS3 gamepad button order. On a Rock Band drum kit the four
// face buttons double as the four coloured pads, and buttons 10/11 are the
// discriminator flags that tell the game whether a given face-button press was
// a drum pad or a cymbal. A press with neither flag set reads as a plain
// gamepad button, which is what menu navigation wants.
// ---------------------------------------------------------------------------
#define MPA_BTN_SQUARE          0   // blue pad / blue cymbal
#define MPA_BTN_CROSS           1   // green pad / green cymbal
#define MPA_BTN_CIRCLE          2   // red pad
#define MPA_BTN_TRIANGLE        3   // yellow pad / yellow cymbal
#define MPA_BTN_KICK            4   // L1 - bass pedal
#define MPA_BTN_KICK2           5   // R1 - second bass pedal
#define MPA_BTN_L2              6
#define MPA_BTN_R2              7
#define MPA_BTN_SELECT          8
#define MPA_BTN_START           9
#define MPA_BTN_PAD_FLAG        10  // "this hit was a drum pad"
#define MPA_BTN_CYMBAL_FLAG     11  // "this hit was a cymbal"
#define MPA_BTN_PS              12  // PS / home button
#define MPA_BUTTON_COUNT        13

// Analog axis indices (report bytes 3..6). Idle/centred value is 0x80.
#define MPA_AXIS_X              0
#define MPA_AXIS_Y              1
#define MPA_AXIS_Z              2
#define MPA_AXIS_RZ             3
#define MPA_AXIS_COUNT          4

// Vendor-defined velocity/pressure bytes (report bytes 7..18). On a genuine
// PS3 Rock Band kit these carry per-pad hit velocity.
#define MPA_VEL_COUNT           12
#define MPA_VEL_GREEN           0
#define MPA_VEL_RED             1
#define MPA_VEL_YELLOW          2
#define MPA_VEL_BLUE            3

#ifdef __cplusplus
extern "C" {
#endif

// Called from usb.c on SET_CONFIGURATION. Not for sketch use.
void usb_mpa_configure(void);

// Clear the button bitmask, centre the hat, and zero the velocity bytes.
// Analog axes and the trailing fixed bytes are left alone.
void usb_mpa_reset_packet(void);

// Set / clear a single button. Out-of-range indices are ignored.
void usb_mpa_set_button(int button);
void usb_mpa_clear_button(int button);

// Set the hat switch. Values above 8 are clamped to MPA_HAT_NEUTRAL.
void usb_mpa_set_hat(uint8_t hat_position);

// Set one of the four 8-bit analog axes (0x80 = centred).
void usb_mpa_set_axis(uint8_t axis, uint8_t value);

// Set one of the twelve vendor velocity bytes.
void usb_mpa_set_velocity(uint8_t index, uint8_t value);

// Transmit the current packet.
//   0  = queued for transmission
//  -1  = not enumerated, or the host stopped polling (transmit timeout)
int usb_mpa_send(void);

// Transmit only if the packet differs from the one last sent. Returns 1 if a
// packet was sent, 0 if nothing changed, negative on error. Use this in a
// tight loop instead of tracking a dirty flag yourself.
int usb_mpa_send_if_changed(void);

extern uint8_t usb_mpa_data[MPA_PACK_SIZE];
extern volatile uint8_t usb_configuration;

#ifdef __cplusplus
}
#endif

#endif // MPA_INTERFACE
#endif // USBmpa_h_
