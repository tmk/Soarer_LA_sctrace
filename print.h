#ifndef print_h__
#define print_h__

#include <avr/pgmspace.h>
#include "usb_debug_only.h"

// this macro allows you to write print("some text") and
// the string is automatically placed into flash memory :)
#define print(s) print_P(PSTR(s))
#define pchar(c) usb_debug_putchar(c)

void print_P(const char *s);
void phex1(unsigned char c); // nibble
void phex(unsigned char c); // byte
void phex16(unsigned int i); // word

#endif
