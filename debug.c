#include "debug.h"
#include "softuart.h"
#include "util.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>

static int dbg_putchar(char c, FILE *stream);

static FILE dbgstdout = FDEV_SETUP_STREAM(dbg_putchar, NULL, _FDEV_SETUP_WRITE);

void debug_init(void){
    PRR &= ~(1 << PRTIM0);
    softuart_init();
    stdout = &dbgstdout;
}

bool debug_char_pending(void) {
    return softuart_kbhit() != 0;
}

char debug_getchar(void) {
    return softuart_getchar();
}

static int dbg_putchar(char c, FILE *stream)
{
    softuart_putchar(c);
    return 0;
}

void debug_putchar(char c) {
    softuart_putchar(c);
    while (softuart_transmit_busy())
        ;
}

void debug_write(const char *str, uint8_t len) {
    while (len > 0) {
        softuart_putchar(*str);
        ++str;
        --len;
    }
}

void debug_finish(void) {
    while (softuart_transmit_busy())
        ;
}

/*
void fatal(const __flash char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf_P(stdout, fmt, ap);
    va_end(ap);
    abort();
}
*/
