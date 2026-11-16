/* Panel LED brightness level (hardware_map.md, PD4 row).
 *
 * The brightness setting is shown at 90 % while PD4 reads high ("full") and at 20 % while it
 * reads low. After boot the level ramps up from dark, and a change of PD4 fades between the two
 * levels at 10 ms per percent (the boot ramp to the low level runs at 20 ms per percent). */
#ifndef LED_LEVEL_H
#define LED_LEVEL_H

#include <stdint.h>

typedef struct {
    uint8_t full;   /* last PD4 state */
    uint8_t mode;   /* 0 idle, 1 boot ramp to low, 2 boot ramp to full, 3 low->full, 4 full->low */
    uint16_t count; /* ms left in the current ramp */
} led_level_t;

void led_level_init(led_level_t *l, int full, uint8_t setting);

/* Call once per millisecond. setting: brightness 10..100 %. Returns the PWM percent. */
uint8_t led_level_tick(led_level_t *l, int full, uint8_t setting);

#endif
