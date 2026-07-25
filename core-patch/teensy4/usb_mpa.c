/* Teensy 4.x MPA (MIDI Pro Adapter) USB device type - implementation
 *
 * See usb_mpa.h for the API and for notes on how this differs from the
 * Teensy 3.x original.
 *
 * Licensed under the same terms as the Teensyduino core library.
 */

#include "usb_dev.h"
#include "usb_mpa.h"
#include "core_pins.h"          // for yield(), delayNanoseconds()
#include <string.h>             // for memcpy(), memset()
#include "avr/pgmspace.h"       // for DMAMEM
#include "debug/printf.h"

#ifdef MPA_INTERFACE // defined by usb_dev.h -> usb_desc.h

// ---------------------------------------------------------------------------
// The report.
//
// Byte  0     : buttons 0-7
// Byte  1     : buttons 8-12 in bits 0-4, bits 5-7 are constant padding
// Byte  2     : hat switch in the low nibble, high nibble constant padding
// Bytes 3-6   : X, Y, Z, Rz - 8 bit, 0x80 = centred
// Bytes 7-18  : twelve vendor-defined bytes (per-pad velocity on a real kit)
// Bytes 19-26 : four 16-bit little-endian vendor fields, idle value 0x0200
//
// Only bytes 0-2 and 7-18 are touched at runtime; the rest are constants
// copied from a genuine Mad Catz adapter.
// ---------------------------------------------------------------------------
uint8_t usb_mpa_data[MPA_PACK_SIZE] = {
	0x00, 0x00,                                     // buttons
	MPA_HAT_NEUTRAL,                                // hat
	0x80, 0x80, 0x80, 0x80,                         // X, Y, Z, Rz
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // vendor 0x20-0x25
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // vendor 0x26-0x2B
	0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02  // vendor 0x2C-0x2F
};

// Snapshot of the last packet handed to the USB hardware, used by
// usb_mpa_send_if_changed().
static uint8_t usb_mpa_last_sent[MPA_PACK_SIZE];
static uint8_t usb_mpa_last_sent_valid = 0;

static uint8_t transmit_previous_timeout = 0;

// When the host isn't listening, how long do we wait before discarding data?
#define TX_TIMEOUT_MSEC 30

#define TX_NUM     4
#define TX_BUFSIZE 32   // must be >= MPA_PACK_SIZE and a multiple of the
                        // 32-byte cache line

#if MPA_PACK_SIZE > TX_BUFSIZE
#error "MPA_PACK_SIZE is larger than the MPA transmit buffer"
#endif

static transfer_t tx_transfer[TX_NUM] __attribute__ ((used, aligned(32)));
DMAMEM static uint8_t txbuffer[TX_NUM * TX_BUFSIZE] __attribute__ ((aligned(32)));
static uint8_t tx_head = 0;

// The genuine adapter also exposes an interrupt OUT endpoint. The PS3 uses it
// for the 8-byte output report (LEDs / rumble on a real controller). We do not
// act on the data, but we must keep a transfer armed: an unconfigured or
// unarmed OUT endpoint NAKs forever, and some hosts treat that as a fault.
// The Teensy 3.x version silently leaked these packets into the shared buffer
// pool, which is why it needed NUM_USB_BUFFERS bumped to 30.
#ifdef MPA_RX_ENDPOINT
#define RX_NUM     2
#define RX_BUFSIZE 64
static transfer_t rx_transfer[RX_NUM] __attribute__ ((used, aligned(32)));
DMAMEM static uint8_t rxbuffer[RX_NUM * RX_BUFSIZE] __attribute__ ((aligned(32)));
static uint8_t rx_head = 0;

static void rx_queue_transfer(int i)
{
	void *buffer = rxbuffer + i * RX_BUFSIZE;
	arm_dcache_delete(buffer, RX_BUFSIZE);
	usb_prepare_transfer(rx_transfer + i, buffer, RX_BUFSIZE, i);
	usb_receive(MPA_RX_ENDPOINT, rx_transfer + i);
}

static void rx_event(transfer_t *t)
{
	// Discard whatever arrived and immediately re-arm the buffer.
	int i = t->callback_param;
	rx_queue_transfer(i);
}
#endif // MPA_RX_ENDPOINT


void usb_mpa_configure(void)
{
	memset(tx_transfer, 0, sizeof(tx_transfer));
	tx_head = 0;
	usb_mpa_last_sent_valid = 0;
	transmit_previous_timeout = 0;
	usb_config_tx(MPA_TX_ENDPOINT, MPA_MAX_SIZE, 0, NULL);
#ifdef MPA_RX_ENDPOINT
	memset(rx_transfer, 0, sizeof(rx_transfer));
	rx_head = 0;
	usb_config_rx(MPA_RX_ENDPOINT, MPA_MAX_SIZE, 0, rx_event);
	for (int i = 0; i < RX_NUM; i++) rx_queue_transfer(i);
#endif
}


void usb_mpa_reset_packet(void)
{
	usb_mpa_data[0] = 0x00;
	usb_mpa_data[1] = 0x00;
	usb_mpa_data[2] = MPA_HAT_NEUTRAL;
	memset(usb_mpa_data + 7, 0x00, MPA_VEL_COUNT);
}


void usb_mpa_set_button(int button)
{
	if (button < 0 || button >= MPA_BUTTON_COUNT) return;
	if (button < 8) usb_mpa_data[0] |= (uint8_t)(1 << button);
	else            usb_mpa_data[1] |= (uint8_t)(1 << (button - 8));
}


void usb_mpa_clear_button(int button)
{
	if (button < 0 || button >= MPA_BUTTON_COUNT) return;
	if (button < 8) usb_mpa_data[0] &= (uint8_t)~(1 << button);
	else            usb_mpa_data[1] &= (uint8_t)~(1 << (button - 8));
}


void usb_mpa_set_hat(uint8_t hat_position)
{
	if (hat_position > MPA_HAT_NEUTRAL) hat_position = MPA_HAT_NEUTRAL;
	usb_mpa_data[2] = hat_position;
}


void usb_mpa_set_axis(uint8_t axis, uint8_t value)
{
	if (axis >= MPA_AXIS_COUNT) return;
	usb_mpa_data[3 + axis] = value;
}


void usb_mpa_set_velocity(uint8_t index, uint8_t value)
{
	if (index >= MPA_VEL_COUNT) return;
	usb_mpa_data[7 + index] = value;
}


int usb_mpa_send(void)
{
	if (!usb_configuration) return -1;
	uint32_t head = tx_head;
	transfer_t *xfer = tx_transfer + head;
	uint32_t wait_begin_at = systick_millis_count;

	// Wait for this slot in the ring to complete. On Teensy 3.x this was a
	// busy-count calibrated per F_CPU, with a lookup table that topped out at
	// 256 MHz - it would not even have compiled at the Teensy 4's 600 MHz.
	// The 4.x core exposes a millisecond counter, so we use real time.
	while (1) {
		uint32_t status = usb_transfer_status(xfer);
		if (!(status & 0x80)) {
			if (status & 0x68) {
				printf("MPA: tx error status = %x, i=%d, ms=%u\n",
					status, tx_head, systick_millis_count);
			}
			transmit_previous_timeout = 0;
			break;
		}
		if (transmit_previous_timeout) return -1;
		if (systick_millis_count - wait_begin_at > TX_TIMEOUT_MSEC) {
			transmit_previous_timeout = 1;
			return -1;
		}
		if (!usb_configuration) return -1;
		yield();
	}
	delayNanoseconds(30); // mirrors usb_joystick.c - status can read ready early

	uint8_t *buffer = txbuffer + head * TX_BUFSIZE;
	memcpy(buffer, usb_mpa_data, MPA_PACK_SIZE);
	usb_prepare_transfer(xfer, buffer, MPA_PACK_SIZE, 0);

	// Mandatory on the IMXRT1062 and absent from the 3.x code: the USB
	// controller DMAs straight out of physical memory, so the buffer has to be
	// pushed out of the data cache first. Skipping this is the classic
	// "works on a 3.6, sends garbage on a 4.1" bug.
	arm_dcache_flush_delete(buffer, TX_BUFSIZE);

	usb_transmit(MPA_TX_ENDPOINT, xfer);
	if (++head >= TX_NUM) head = 0;
	tx_head = head;

	memcpy(usb_mpa_last_sent, usb_mpa_data, MPA_PACK_SIZE);
	usb_mpa_last_sent_valid = 1;
	return 0;
}


int usb_mpa_send_if_changed(void)
{
	if (usb_mpa_last_sent_valid &&
	    memcmp(usb_mpa_last_sent, usb_mpa_data, MPA_PACK_SIZE) == 0) {
		return 0;
	}
	int r = usb_mpa_send();
	return (r < 0) ? r : 1;
}

#endif // MPA_INTERFACE
