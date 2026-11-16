#include "touch_strip.h"

/* Offsets from centre for the release spring, from the stock firmware (keyboard.md section 9). */
static const uint8_t spring[17] = {0, 2, 4, 5, 7, 8, 10, 13, 15, 17, 20, 24, 28, 33, 39, 48, 64};

uint8_t touch_scale(uint8_t raw, touch_cal_t cal)
{
    if (raw == 0)
        return TOUCH_NONE;
    int span = (int)cal.hi - (int)cal.lo;
    if (span == 0)
        return 0;
    int v = (((int)raw - (int)cal.lo) * 0x7F0 / span) >> 4;
    return (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
}

uint8_t touch_pitch_map(uint8_t s)
{
    if (s < 55)
        return (uint8_t)(s * 64 / 54);
    if (s < 74)
        return 64;
    int d = s - 74;
    return (uint8_t)(64 + d * 127 / 53 - d * 64 / 53);
}

void pitch_strip_init(pitch_strip_t *p)
{
    *p = (pitch_strip_t){0};
    p->last = 0xFF;
}

int pitch_strip_update(pitch_strip_t *p, uint8_t raw, touch_cal_t cal)
{
    uint8_t s = touch_scale(raw, cal);
    if (s == TOUCH_NONE) {
        if (!p->active)
            return -1;
        if (p->spring_idx == 0) {
            p->active = 0;
            p->last = 64;
            return 64;
        }
        p->spring_idx--;
        uint8_t off = spring[p->spring_idx];
        p->last = (uint8_t)(p->spring_up ? 64 + off : 64 - off);
        return p->last;
    }
    uint8_t v = touch_pitch_map(s);
    if (v == p->last)
        return -1;
    p->last = v;
    p->active = v != 64;
    /* spring start position for a release from here */
    if (v < 64) {
        p->spring_idx = (uint8_t)((64 - v) >> 2);
        p->spring_up = 0;
    } else {
        p->spring_idx = (uint8_t)((v >> 2) - 16);
        p->spring_up = 1;
    }
    return v;
}

void mod_strip_init(mod_strip_t *m)
{
    m->last = 0xFF;
}

int mod_strip_update(mod_strip_t *m, uint8_t raw, touch_cal_t cal)
{
    uint8_t s = touch_scale(raw, cal);
    if (s == TOUCH_NONE || s == m->last)
        return -1;
    m->last = s;
    return s;
}
