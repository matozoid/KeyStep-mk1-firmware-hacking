#include "led_level.h"

static unsigned hi_of(uint8_t s) { return s * 90u / 100u; }
static unsigned lo_of(uint8_t s) { return s * 20u / 100u; }

void led_level_init(led_level_t *l, int full, uint8_t setting)
{
    l->full = full ? 1 : 0;
    if (l->full) {
        l->mode = 2;
        l->count = (uint16_t)(hi_of(setting) * 10u);
    } else {
        l->mode = 1;
        l->count = (uint16_t)(lo_of(setting) * 20u);
    }
}

uint8_t led_level_tick(led_level_t *l, int full, uint8_t setting)
{
    unsigned hi = hi_of(setting), lo = lo_of(setting), pct;

    full = full ? 1 : 0;
    if (full != l->full) {
        l->full = (uint8_t)full;
        l->mode = full ? 3 : 4;
        l->count = (uint16_t)((hi - lo) * 10u);
    }
    if (l->count)
        l->count--;

    switch (l->mode) {
    case 1: pct = lo - l->count / 20u; break;
    case 2:
    case 3: pct = hi - l->count / 10u; break;
    case 4: pct = lo + l->count / 10u; break;
    default: pct = 0; break;
    }
    if (!l->count)
        l->mode = 0;
    if (!l->mode) {
        pct = l->full ? hi : lo;
        if (!pct)
            pct = 1;
    }
    return (uint8_t)pct;
}
