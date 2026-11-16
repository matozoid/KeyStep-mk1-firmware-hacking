#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

/* Shift-register bit per LED (Docs/re/hardware_map.md). leds_write() takes 1 = lit. */
enum led {
    LED_STOP = 0,
    LED_PLAY = 1,
    LED_HOLD = 2,
    LED_OCT_UP = 3,
    LED_OCT_DOWN = 4,
    LED_REC = 5,
    LED_TAP = 6,
    LED_SHIFT = 7,
};

void leds_init(void);
void leds_write(uint8_t bits);

#endif
