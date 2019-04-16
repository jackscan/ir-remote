#ifndef _UTIL_H_
#define _UTIL_H_

#include <avr/io.h>
#include <avr/pgmspace.h>

#include <util/delay.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#define FSTR(str) ({static const __flash char s[] = str; &s[0];})

static inline void setpin(volatile uint8_t *port, uint8_t pin, bool value)
    __attribute((always_inline));

static inline void setpin(volatile uint8_t *port, uint8_t pin, bool value) {
    if (value)
        *port |= (1 << pin);
    else
        *port &= ~(1 << pin);
}

// static inline void blink(uint8_t on, uint8_t off, uint8_t count) __attribute((always_inline));
// static inline void blink(uint8_t on, uint8_t off, uint8_t count) {
//     for (uint8_t i = 0; i < count; ++i) {
//         setpin(&LED_PORT, LED_PIN, true);
//         _delay_ms(on);
//         setpin(&LED_PORT, LED_PIN, false);
//         _delay_ms(off);
//     }
// }

static inline char *strprefix(char *str, const __flash char *prefix) {
	const uint8_t n = strlen_P(prefix);
	if (strncmp_P(str, prefix, n))
		return NULL;

	return str + n;
}

#define GET_SP() ({ \
    void *temp; \
    __asm__ __volatile__ ( \
        "in %A0, %A1" "\n\t" \
        "in %B0, %B1" "\n\t" \
        : "=r" (temp) \
        : "I" ((_SFR_IO_ADDR(SP))) \
    ); \
    temp; \
})

/*#define GET_SP() ({ \
    uint8_t temp; \
    &temp; \
})*/

#endif
