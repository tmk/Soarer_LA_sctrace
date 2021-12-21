// Simple interrupt-driven logic tracer for use with Soarer's Converter.
//
//	(C) 2012 Soarer (on DeskThority & Geekhack forums)
//
// Main purpose is analyzing/checking keyboard protocols.
// Captures port D state whenever the state of PD0/PD1/PD2/PD3 changes.
// (Compilation option to capture port B state whenever any pin on port B changes instead).
// Capturing an event takes 25 cycles (just over 1.5us).
// Main limitation is the output (using debug print channel) which can
// only send 4000 events per second.

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "usb_debug_only.h"
#include "print.h"
#include "register_vars.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Configuration...

// Valid capture port settings are 'D' or 'B'.
// 'D' uses external interrupts INT0 to INT 3.
// 'B' uses the pin change interrupt, triggering on all 8 pins.
#ifndef CAPTURE_PORT
#define CAPTURE_PORT 'D'
#endif

// If 1, outputs a reset signal on PB7 (needed to init some PC/XT keyboards).
#ifndef RESET_OUTPUT_ENABLE
#define RESET_OUTPUT_ENABLE 1
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#if CAPTURE_PORT == 'D'
#define CAPTURE_PORT_IN			PIND
#define INTERRUPT_FLAG_REG		EIFR
#define INTERRUPT_FLAG_CLEAR	0x0F
#elif CAPTURE_PORT == 'B'
#define CAPTURE_PORT_IN			PINB
#define INTERRUPT_FLAG_REG		PCIFR
#define INTERRUPT_FLAG_CLEAR	0x01
#else
#error "Invalid capture port setting"
#endif

#if CAPTURE_PORT == 'B' && RESET_OUTPUT_ENABLE
#warning "Reset output cannot be enabled while capturing on Port B"
#undef RESET_OUTPUT_ENABLE
#define RESET_OUTPUT_ENABLE 0
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define CPU_PRESCALE(n)	(CLKPR = 0x80, CLKPR = (n))

#ifndef RAMSTART
#define RAMSTART 0x100
#endif

#ifndef RAM_SIZE
#define RAM_SIZE (RAMEND - RAMSTART + 1)
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// input queue

// high byte of input queue address...
#define IQPAGE 1

#define IQENTRYSZ 4

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

inline char hex(uint8_t v)
{
	return (v + ((v < 10) ? '0' : 'A' - 10));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// output queue

// Let output queue use all of RAM except for input queue (256 bytes),
// and other data variables + stack (256 bytes is generous)...
#define OQENTRYSZ 4
#define OQSZ (((RAM_SIZE - 512) / OQENTRYSZ) * OQENTRYSZ)

uint8_t oqueue[OQSZ];
uint8_t* oqhead = oqueue;
uint8_t* oqtail = oqueue;

inline uint8_t* oqnext(uint8_t* p)
{
	uint8_t* pnext = p + OQENTRYSZ;
	if ( pnext == &oqueue[OQSZ] ) {
		pnext = oqueue;
	}
	return pnext;
}

inline uint8_t oqpush(uint8_t v1, uint8_t v2, uint8_t v3, uint8_t v4)
{
	uint8_t* p = oqnext(oqhead);
	if ( p == oqtail ) {
		return 0; // full
	}
	oqhead[0] = v1;
	oqhead[1] = v2;
	oqhead[2] = v3;
	oqhead[3] = v4;
	oqhead = p;
	return 1; // ok
}

inline uint8_t oqempty(void)
{
	return oqhead == oqtail;
}

inline uint8_t oqpop(uint8_t* v1, uint8_t* v2, uint8_t* v3, uint8_t* v4)
{
	if ( oqempty() ) {
		return 0; // empty
	}
	*v1 = oqtail[0];
	*v2 = oqtail[1];
	*v3 = oqtail[2];
	*v4 = oqtail[3];
	oqtail = oqnext(oqtail);
	return 1; // ok
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// main

int main(void)
{
	CPU_PRESCALE(0);

	// Setup USB...
	usb_init();
	while ( !usb_configured() ) {}
	_delay_ms(1000);

#if RESET_OUTPUT_ENABLE
	// Output a reset signal (needed to init some PC/XT keyboards)...
	PORTB &= ~0x80;
	DDRB |= 0x80; // output -reset on PB7
	_delay_ms(500);
	DDRB &= ~0x80; // clear -reset
	PORTB |= 0x80;
#endif

	// Init buffer for events from the ISRs...
	// Makefile has this line to move the start of data forward by the 256 needed for the queue:
	// LDFLAGS += -Wl,--section-start,.data=0x800200
	// The queue is locked to 0x0100 so that wrapping the qhead and qtail variables is trivial.
	uint8_t* iqueue = (uint8_t*)(IQPAGE << 8);
	iqpage = IQPAGE;
	iqhead = 0;
	uint8_t iqtail = 0;

	// Set register used to clear pending interrupts in ISR...
	eifrclr = INTERRUPT_FLAG_CLEAR;

	// Setup inputs and interrupts...
#if CAPTURE_PORT == 'D'
	// .. for INT0 to INT3 pins...
	DDRD = 0; // may as well input the entire port
	PORTD = 0xFF; // set pull-ups on
	EICRA = 0x55; // trigger on either edge
	EIFR = eifrclr; // clear pending
	EIMSK |= 0x0F; // enable
#else // 'B'
	// .. for PCINT pins...
	DDRB = 0; // input the entire port
	PORTB = 0xFF; // set pull-ups on
	PCICR = 0x01; // enable
	PCIFR = eifrclr; // clear pending
	PCMSK0 |= 0xFF; // enable all
#endif

	uint8_t prev_pv = CAPTURE_PORT_IN;

	// Setup Timer 1 for the capture event timebase...
	TCCR1A = 0x00; // set timer 1 to normal mode
	TCCR1B = 0x01; // start timer running at clock speed
	//TCCR1B = 0x02; // start timer running at clock speed / 8
	TIMSK1 |= (1 << TOIE1); // enable overflow interrupt

	// Buffer for formatted text ready for output...
	static char obuf[16];
	uint8_t obuf_idx = 0;
	obuf[obuf_idx] = 0;

	print("sctrace v1.01\n");
	const uint8_t max_timer_events = 2;
	uint8_t allow_timer_events = max_timer_events;
	while ( 1 ) {
		// Move from the input queue to the larger output queue, skipping excess timer events...
		if ( iqhead != iqtail ) { // if input queue isn't empty
			uint8_t tlo = iqueue[iqtail];
			uint8_t thi = iqueue[iqtail+1];
			uint8_t pv = iqueue[iqtail+2];
			uint8_t is_timer_event = !iqueue[iqtail+3];
			iqtail += IQENTRYSZ;
			//uint8_t is_timer_event = (pv == prev_pv) && (thi == 0);
			prev_pv = pv;
			if ( is_timer_event ) {
				if ( allow_timer_events ) {
					oqpush(tlo, thi, pv, is_timer_event);
					--allow_timer_events;
				}
			} else {
				oqpush(tlo, thi, pv, is_timer_event);
				allow_timer_events = max_timer_events;
			}
		}

		// Move from the output queue to the formatted output buffer...
		if ( !obuf[obuf_idx] && !oqempty() ) {
			uint8_t tlo, thi, pv, tf;
			oqpop(&tlo, &thi, &pv, &tf);
			uint8_t i = 0;
			obuf[i++] = hex(thi >> 4);
			obuf[i++] = hex(thi & 0x0F);
			obuf[i++] = hex(tlo >> 4);
			obuf[i++] = hex(tlo & 0x0F);
			obuf[i++] = hex(pv >> 4);
			obuf[i++] = hex(pv & 0x0F);
			obuf[i++] = hex(tf & 0x01);
			const uint8_t items_per_line = 10;
			static uint8_t remaining = items_per_line;
			if ( --remaining ) {
				obuf[i++] = ' ';
			} else {
				obuf[i++] = '\n';
				remaining = items_per_line;
			}
			obuf[i++] = 0;
			obuf_idx = 0;
		}

		// Send from the formatted output buffer...
		if ( obuf[obuf_idx] && usb_debug_ready() ) {
			usb_debug_putchar(obuf[obuf_idx++]);
		}

		// Allow flushing of debug output without using an ISR, since that
		// could block our capture ISRs...
		usb_debug_task();
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ISRs

#define _CAPTURE_ISR(capflag) asm volatile \
( \
	"out %[eifr], r6"		"\n\t"	/* clear pending external interrupts (note 1) */ \
	"in r3, %[pin]"			"\n\t"	/* read port (note 2) */ \
	"lds r4, %[tcnt1l]"		"\n\t"	/* read timer lo */ \
	"lds r5, %[tcnt1h]"		"\n\t"	/* read timer hi */ \
	"st X+, r4"				"\n\t"	/* store timer lo */ \
	"st X+, r5"				"\n\t"	/* store timer hi */ \
	"st X+, r3"				"\n\t"	/* store port state */ \
	"st X+, " #capflag		"\n\t"	/* store non-zero byte to indicate capture */ \
	"ldi r27, %[iqpage]"	"\n\t"	/* reset high byte of X */ \
	"reti"					"\n\t" \
	: \
	: [tcnt1l] "X" (TCNT1L), [tcnt1h] "X" (TCNT1H), [iqpage] "X" (IQPAGE), \
	  [pin] "I" (_SFR_IO_ADDR(CAPTURE_PORT_IN)), [eifr] "I" (_SFR_IO_ADDR(INTERRUPT_FLAG_REG)) \
)

#define TIMER_ISR() _CAPTURE_ISR(__zero_reg__)
#define CAPTURE_ISR() _CAPTURE_ISR(r6)

#define OLD_CAPTURE_ISR() asm volatile \
( \
	"out %[eifr], r6"		"\n\t"	/* clear pending external interrupts (note 1) */ \
	"in r3, %[pin]"			"\n\t"	/* read port (note 2) */ \
	"lds r4, %[tcnt1l]"		"\n\t"	/* read timer lo */ \
	"lds r5, %[tcnt1h]"		"\n\t"	/* read timer hi */ \
	"st X+, r4"				"\n\t"	/* store timer lo */ \
	"st X+, r5"				"\n\t"	/* store timer hi */ \
	"st X+, r3"				"\n\t"	/* store port state */ \
	"in r2, __SREG__"		"\n\t"	/* save flags */ \
	"inc r26"				"\n\t"	/* increment qhead with 8-bit wrap-around */ \
	"out __SREG__, r2"		"\n\t"	/* restore flags */ \
	"reti"					"\n\t" \
	: \
	: [tcnt1l] "X" (TCNT1L), [tcnt1h] "X" (TCNT1H), [pin] "I" (_SFR_IO_ADDR(CAPTURE_PORT_IN)), [eifr] "I" (_SFR_IO_ADDR(INTERRUPT_FLAG_REG)) \
)

// Notes:
//
// 1. Pending external interrupts are cleared to avoid having to process
//	multiple interrupts when simultaneous changes occur on multiple input
//	pins. There is still a very small chance that a further change will
//	happen between clearing the interrupts and reading the port.
//
// 2. The port is read before the timer because offsetting the timer by any
//	constant time is irrelevant.

ISR(TIMER1_OVF_vect, ISR_NAKED) { TIMER_ISR(); }

ISR(INT0_vect, ISR_NAKED) { CAPTURE_ISR(); }
ISR(INT1_vect, ISR_NAKED) { CAPTURE_ISR(); }
ISR(INT2_vect, ISR_NAKED) { CAPTURE_ISR(); }
ISR(INT3_vect, ISR_NAKED) { CAPTURE_ISR(); }

ISR(PCINT0_vect, ISR_NAKED) { CAPTURE_ISR(); }
