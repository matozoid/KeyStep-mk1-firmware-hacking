#include "clock.h"

/* Ticks per minute = bpm100 / 100 * 480; per microsecond = bpm100 * 480 / (100 * 60e6). The
 * accumulator counts in units of 1/(bpm100*480) tick-microseconds. */
#define ACC_PER_TICK 6000000000ull /* 100 * 60e6 */

static const uint16_t sync_ticks[5] = {20, 120, 240, 20, 10};

uint16_t clock_sync_ticks(uint8_t type)
{
    return sync_ticks[type < 5 ? type : 1];
}

void clock_init(mclock_t *c)
{
    *c = (mclock_t){0};
    c->bpm100 = 12000;
    c->sync_out = SYNC_OUT_1STEP_CLOCK;
    c->tap_avg = 2;
    c->ext_ticks_per_pulse = CLOCK_TICKS_PER_F8;
}

void clock_set_bpm100(mclock_t *c, uint32_t bpm100)
{
    if (bpm100 < CLOCK_BPM100_MIN)
        bpm100 = CLOCK_BPM100_MIN;
    if (bpm100 > CLOCK_BPM100_MAX)
        bpm100 = CLOCK_BPM100_MAX;
    c->bpm100 = (uint16_t)bpm100;
}

void clock_set_source(mclock_t *c, uint8_t source, uint16_t ticks_per_pulse)
{
    c->source = source;
    c->ext_ticks_per_pulse = ticks_per_pulse ? ticks_per_pulse : CLOCK_TICKS_PER_F8;
    c->ext_period_us = 0;
    c->ext_done = 0;
    c->ext_seen = 0;
}

void clock_reset(mclock_t *c, uint32_t now_us)
{
    c->tick = 0;
    c->acc = 0;
    c->last_us = now_us;
    c->ext_done = 0;
    c->ext_seen = 0;
}

unsigned clock_poll(mclock_t *c, uint32_t now_us)
{
    if (c->source == CLOCK_INTERNAL) {
        uint32_t dt = now_us - c->last_us;
        c->last_us = now_us;
        c->acc += (uint64_t)dt * c->bpm100 * CLOCK_PPQN;
        unsigned n = (unsigned)(c->acc / ACC_PER_TICK);
        c->acc -= (uint64_t)n * ACC_PER_TICK;
        return n;
    }
    /* external: interpolate, but never run a whole pulse ahead */
    if (!c->ext_period_us)
        return 0;
    uint32_t el = now_us - c->ext_last_pulse_us;
    uint32_t due = (uint32_t)((uint64_t)el * c->ext_ticks_per_pulse / c->ext_period_us);
    if (due > (uint32_t)c->ext_ticks_per_pulse - 1u)
        due = c->ext_ticks_per_pulse - 1u;
    if (due <= c->ext_done)
        return 0;
    unsigned n = due - c->ext_done;
    c->ext_done = (uint16_t)due;
    return n;
}

unsigned clock_ext_pulse(mclock_t *c, uint32_t now_us)
{
    unsigned n = 1; /* the pulse's own tick */
    if (c->ext_seen) {
        n += c->ext_ticks_per_pulse - 1u - c->ext_done; /* ticks interpolation did not reach */
        uint32_t period = now_us - c->ext_last_pulse_us;
        if (period > 0 && period < 2000000u) {
            c->ext_period_us = period;
            uint64_t bpm100 = 6000000000ull * c->ext_ticks_per_pulse / CLOCK_PPQN / period;
            c->bpm100 = (uint16_t)(bpm100 > 65535u ? 65535u : bpm100);
        } else {
            c->ext_period_us = 0; /* restarted after a pause: wait for the next interval */
        }
    }
    c->ext_seen = 1;
    c->ext_last_pulse_us = now_us;
    c->ext_done = 0;
    return n;
}

unsigned clock_output(mclock_t *c)
{
    unsigned f = 0, t = c->tick;
    uint16_t sp = clock_sync_ticks(c->sync_out);

    if (t % CLOCK_TICKS_PER_F8 == 0)
        f |= CLOCK_OUT_MIDI_CLOCK;
    if (c->sync_out != SYNC_OUT_1STEP_GATE) {
        if (t % sp == 0)
            f |= CLOCK_OUT_SYNC_HIGH;
        else if (t % sp == sp / 2u)
            f |= CLOCK_OUT_SYNC_LOW;
    }
    if (t % CLOCK_PPQN == 0)
        f |= CLOCK_OUT_BEAT_ON;
    else if (t % CLOCK_PPQN == CLOCK_PPQN / 2u)
        f |= CLOCK_OUT_BEAT_OFF;
    c->tick++;
    return f;
}

uint32_t clock_tap(mclock_t *c, uint32_t now_us)
{
    if (c->tap_n && now_us - c->tap_us[c->tap_n - 1] > 2000000u)
        c->tap_n = 0; /* too long since the last tap: start over */
    if (c->tap_n == TAP_MAX) {
        for (unsigned i = 1; i < TAP_MAX; i++)
            c->tap_us[i - 1] = c->tap_us[i];
        c->tap_n--;
    }
    c->tap_us[c->tap_n++] = now_us;
    if (c->tap_n < 2)
        return 0;
    unsigned k = c->tap_n - 1u;
    if (k > c->tap_avg)
        k = c->tap_avg;
    uint32_t span = now_us - c->tap_us[c->tap_n - 1u - k];
    uint32_t interval = span / k;
    if (!interval)
        return 0;
    clock_set_bpm100(c, (uint32_t)(6000000000ull / interval));
    return c->bpm100;
}
