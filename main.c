/* Name: main.c
 * Author: <insert your name here>
 * Copyright: <insert your copyright message here>
 * License: <insert your license reference here>
 */

#include "debug.h"
#include "util.h"

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include <stdio.h>

#define ROT_PORT PORTB
#define ROT_DDR DDRB
#define ROT_PIN PINB
#define ROT_SHIFT PB3
#define BTN_PCMSK (1 << PCINT2)
#define BTN_BIT (1 << PB2)
#define ROT_BITS ((1 << PB4) | (1 << PB3))
#define ROT_PCMSK ((1 << PCINT3) | (1 << PCINT4))
#define ROT_STEPS 20

#define IR_TIMER_COMP (1 << COM0B1)
#define IR_TIMER_TOP OCR0A
#define IR_TIMER_DUTY_START OCR0B
#define IR_TIMER_START do { \
    TIMSK |= (1 << TOIE0); \
    TCCR0A = (1 << WGM01) | (1 << WGM00); \
    TCCR0B = (1 << WGM02) | (1 << CS00); \
} while (false)
#define IR_TIMER_CNT TCNT0
#define IR_TIMER_STOP do { \
    TIMSK &= ~(1 << TOIE0); \
    TCCR0B &= ~(1 << CS00); \
} while (false)
#define IR_TIMER_PWM_ON TCCR0A |= ((1 << COM0B1) | (1 << COM0B0))
#define IR_TIMER_PWM_OFF TCCR0A &= ~((1 << COM0B1) | (1 << COM0B0))
#define IR_PORT PORTB
#define IR_DDR DDRB
#define IR_BIT (1 << PB1)

static struct {
    volatile int8_t dir;
    volatile bool pressed;
    uint8_t state;
} s_rotbtn;

static void early_init(void) {
    cli();
    // configure clock to 4Mhz
    CLKPR = (1 << CLKPCE); // change protection
    CLKPR = (1 << CLKPS0); // divide by 2

    // disable all peripherals
    PRR = (1 << PRTIM1) | (1 << PRTIM0) | (1 << PRUSI) | (PRADC);
}

ISR(ADC_vect) {
    // we need this interrupt only for wake up during SLEEP_MODE_ADC
}

ISR(PCINT0_vect) {
    s_rotbtn.pressed = (ROT_PIN & BTN_BIT) == 0;
    uint8_t state = (ROT_PIN >> ROT_SHIFT) & 0x3;
    uint8_t n = (((~state) & 1) << 1) | (state >> 1);
    uint8_t d = (s_rotbtn.state ^ n);
    s_rotbtn.state = state;

    if (d == 0x3) ++s_rotbtn.dir;
    if (d == 0x0) --s_rotbtn.dir;
}

static void rotbutton_init(void) {
    DDRB &= ~(BTN_BIT | ROT_BITS);
    PORTB |= BTN_BIT;
    PCMSK |= ROT_PCMSK | BTN_PCMSK;
    GIMSK |= (1 << PCIE);
}

static uint16_t check_voltage(void) {
    // disable power reduction for ADC
    PRR &= ~(1 << PRADC);

    // select VCC as reference
    // left adjusted result
    // mux select V_BG
    ADMUX = 0 | (1 << ADLAR) | (1 << MUX3) | (1 << MUX2);

    // choose prescaler for at least 50kHz
    uint8_t scale = 7;
    while (scale > 2 && (1 << scale) > (F_CPU / 50000))
        --scale;

    // wait 10ms for internal reference voltage to settle
    _delay_ms(10);

    // enable ADC with prescaler
    ADCSRA = (1 << ADEN) | scale;

    // wait while ADC is busy
    while ((ADCSRA & (1 << ADSC)) != 0)
        ;

    // start conversion
    ADCSRA |= (1 << ADSC) | (1 << ADIE);

    while (true) {
        set_sleep_mode(SLEEP_MODE_ADC);
        // check updates
        cli();
        if ((ADCSRA & (1 << ADSC)) != 0) {
            // sleep while conversion is running
            sleep_enable();
            sei();
            sleep_cpu();
            sleep_disable();
        } else {
            sei();
            break;
        }
    }

    // result
    uint8_t ref11 = ADCH; // = 255 * 1.1V / Vbat

    // disable ADC
    ADCSRA = 0; //~(1 << ADEN);
    PRR |= (1 << PRADC);

    // Vbat*100 = 255*1.1*100 / ref11
    uint16_t result = (uint16_t)28050 / (uint16_t)ref11;

    return result;
}

static void powerdown(void) {

    LOG("powerdown\n");
    debug_finish();

    // stop timer0
    TCCR0B = 0;
    // stop timer1
    TCCR1 = 0;

    // set all to input
    // DDRB = 0;
    // disable any pullups
    // PORTB = 0;

    // disable all digital inputs
    // DIDR0 = (1 << ADC0D) | (1 << ADC2D) | (1 << ADC3D) | (1 << ADC1D) |
    //         (1 << AIN1D) | (1 << AIN0D);

    uint8_t didr0 = DIDR0;
    // disable digital inputs on led pins
    DIDR0 = didr0 | (1 << AIN1D) | (1 << AIN0D);

    // disable all peripherals
    PRR = (1 << PRTIM1) | (1 << PRTIM0) | (1 << PRUSI) | (PRADC);

    // PORTB |= (1 << PB3);
    // PCMSK = (1 << PCINT3);
    // GIMSK = (1 << PCIE);
    // DIDR0 &= ~(1 << ADC3D);

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    cli();
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();

    DIDR0 = didr0;
    debug_init();

    LOG("wakeup\n");
}

static struct {
    volatile uint64_t data;
    volatile uint8_t datalen;
    volatile uint8_t pulsecount;
    volatile uint8_t seqlen;
} s_irtimer;

ISR(TIMER0_COMPB_vect) {
    if (s_irtimer.pulsecount == 0) {
        if (s_irtimer.datalen == 0) {
            IR_TIMER_STOP;
            return;
        }
        --s_irtimer.datalen;
        s_irtimer.pulsecount = s_irtimer.seqlen;
        if ((s_irtimer.data & 1) != 0)
            IR_TIMER_PWM_ON;
        else
            IR_TIMER_PWM_OFF;
        s_irtimer.data >>= 1;
    }
    --s_irtimer.pulsecount;
}

static void start_pwm(uint16_t freq) {
    uint8_t top = (F_CPU + freq) / ((uint32_t)freq * 2) - 1;
    IR_TIMER_TOP = top;
    IR_TIMER_DUTY_START = top - top / 4; // 1/4 duty cycle
    IR_TIMER_CNT = 0;
    IR_PORT &= ~IR_BIT; // set low
    IR_DDR |= IR_BIT; // set to output
    IR_TIMER_START;
}

static void ir_wait_for_finish(void) {
    // wait for last data to finish
    set_sleep_mode(SLEEP_MODE_IDLE);
    cli();
    while (s_irtimer.datalen > 0) {
        sleep_enable();
        sei();
        sleep_cpu();
        sleep_disable();
        cli();
    }
    sei();
}

static void send_rc5(uint32_t data) {
    // Each data bit represents one period of 32/36kHz.
    // Transmit order is from LSB to MSB.

    // RC5 command format:
    // Each bit consists of two periods of 32/36kHz.
    // 1: burst of 32 pulses, silence
    // 0: silence, burst of 32 pulses
    // two 1s, toggle bit, 5 adressbits

    static bool toggle = false;
    if (toggle) data ^= 0b110000;
    toggle = !toggle;

    ir_wait_for_finish();

    s_irtimer.datalen = 14 * 2;
    s_irtimer.seqlen = 32;
    s_irtimer.data = data;

    start_pwm(36000);
}

static void send_sony(uint64_t data) {
    for (uint8_t i = 0; i < 3; ++i) {
        ir_wait_for_finish();
        // 600us, 45ms total
        s_irtimer.datalen = 45000/600;
        s_irtimer.seqlen = 24;
        s_irtimer.data = data;
        start_pwm(40000);
    }
}

static void loop(void) {
    for (;;) {
        while (debug_char_pending()) {
            LOG(">\n");
            char c = debug_getchar();
            switch (c) {
            case 'v': {
                uint16_t v = check_voltage();
                div_t d = div((int)v, 100);
                LOG("volt: %i.", d.quot);
                d = div(d.rem, 10);
                LOG("%i%i\n", d.quot, d.rem);
            } break;

            case 'p': powerdown(); break;

            default: LOG("unkown\n"); break;
            }
        }

        static uint8_t rotstate = 0;

        if (rotstate != s_rotbtn.state) {
            rotstate = s_rotbtn.state;
            LOG("state: 0x%x\n", rotstate);
        }

        if (s_rotbtn.dir != 0 || s_rotbtn.pressed) {
            cli();
            int8_t dir = s_rotbtn.dir;
            s_rotbtn.dir = 0;
            bool pressed = s_rotbtn.pressed;
            // s_rotbtn.pressed = false;
            sei();
            LOG("dir: %d\n", dir);
            if (pressed) LOG("press\n");
        }

        set_sleep_mode(SLEEP_MODE_IDLE);
        // check updates
        cli();
        if (!debug_char_pending()) {
            // sleep when no updates
            sleep_enable();
            sei();
            sleep_cpu();
            sleep_disable();
        } else {
            sei();
        }
    }
}

int main(void) {
    // save reset reason
    uint8_t mcusr = MCUSR;
    // clear reset flags for next reset
    MCUSR = 0;

    early_init();

    // PORTB = (1 << PB4);
    // DDRB = (1 << PB4);
    // _delay_ms(100);
    // PORTB = 0;
    // DDRB = 0;

    // init
    {
        debug_init();
        rotbutton_init();
        sei();
    }

    LOG("\nreset: 0x%x\n", mcusr);
    LOG("voltage: %u\n", check_voltage());
    LOG("OSCAL: 0x%x\n", OSCCAL);
    loop();

    return 0; /* never reached */
}
