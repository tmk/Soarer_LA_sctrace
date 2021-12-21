// Variables in registers to allow using naked ISRs.
// This file MUST be included in ALL source files!
// Register assignments must match the assembly code in the ISRs!
//volatile register uint8_t flags		asm("r2");		// temporary for SREG during ISRs
//volatile register uint8_t qtail		asm("r3");	// global tail of queue
volatile register uint8_t pinstate	asm("r3");		// temporary for PIND during ISRs
volatile register uint8_t tcnt1l	asm("r4");		// temporary for TCNT1L during ISRs
volatile register uint8_t tcnt1h	asm("r5");		// temporary for TCNT1H during ISRs
volatile register uint8_t eifrclr	asm("r6");		// global constant for clearing EIFR in ISRs
// Some operations (e.g. andi) can only be performed on registers r16 and up...
//volatile register uint8_t pinstate	asm("r16");		// temporary for PIND during ISRs
// The X pointer register is r26 and r27...
volatile register uint8_t iqhead		asm("r26");		// global head of queue
volatile register uint8_t iqpage		asm("r27");		// global (constant) hi-byte of queue address
