#ifndef usb_serial_h__
#define usb_serial_h__

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>

void usb_init(void);			// initialize everything
uint8_t usb_configured(void);		// is the USB port configured

int8_t usb_debug_putchar(uint8_t c);	// transmit a character
void usb_debug_flush_output(void);	// immediately transmit any buffered output
#define USB_DEBUG_HID

#define DEBUG_TX_ENDPOINT	3

void usb_debug_task(void);

inline uint8_t usb_debug_ready(void)
{
// 	uint8_t intr_state = SREG;
// 	cli();
	UENUM = DEBUG_TX_ENDPOINT;
	uint8_t retval = UEINTX & (1<<RWAL);
// 	SREG = intr_state;
	return retval;
}

#endif
