#include "test.h"
#include "../src/core/clock.h"

static void test_internal(void)
{
    mclock_t c;
    clock_init(&c); /* 120 BPM = 960 ticks/s */
    clock_reset(&c, 0);
    unsigned total = 0;
    for (uint32_t us = 0; us <= 1000000; us += 137)
        total += clock_poll(&c, us);
    CHECK(total >= 959 && total <= 960);
    clock_set_bpm100(&c, 1000);
    CHECK_EQ(c.bpm100, CLOCK_BPM100_MIN);
}

static void test_external(void)
{
    mclock_t c;
    clock_init(&c);
    clock_set_source(&c, CLOCK_MIDI, CLOCK_TICKS_PER_F8);
    clock_reset(&c, 0);
    /* 120 BPM MIDI clock: 0xF8 every 20833 us */
    unsigned total = 0;
    uint32_t us = 0;
    for (int pulse = 0; pulse < 48; pulse++) {
        total += clock_ext_pulse(&c, us);
        for (uint32_t t = us + 100; t < us + 20833; t += 100)
            total += clock_poll(&c, t);
        us += 20833;
    }
    total += clock_ext_pulse(&c, us);
    CHECK_EQ(total, 48 * 20 + 1);
    CHECK(c.bpm100 >= 11990 && c.bpm100 <= 12010);

    /* slower pulse than expected: interpolation stops one tick short, rest caught up */
    unsigned n = 0;
    for (uint32_t t = us + 100; t < us + 50000; t += 100)
        n += clock_poll(&c, t);
    CHECK_EQ(n, 19);
    CHECK_EQ(clock_ext_pulse(&c, us + 50000), 1);
}

static void test_outputs(void)
{
    mclock_t c;
    clock_init(&c);
    c.sync_out = SYNC_OUT_1STEP_CLOCK;
    unsigned f8 = 0, hi = 0, lo = 0, beat = 0;
    for (int t = 0; t < 960; t++) {
        unsigned f = clock_output(&c);
        f8 += !!(f & CLOCK_OUT_MIDI_CLOCK);
        hi += !!(f & CLOCK_OUT_SYNC_HIGH);
        lo += !!(f & CLOCK_OUT_SYNC_LOW);
        beat += !!(f & CLOCK_OUT_BEAT_ON);
    }
    CHECK_EQ(f8, 48);
    CHECK_EQ(hi, 8);
    CHECK_EQ(lo, 8);
    CHECK_EQ(beat, 2);
}

static void test_tap(void)
{
    mclock_t c;
    clock_init(&c);
    c.tap_avg = 3;
    CHECK_EQ(clock_tap(&c, 1000000), 0);
    CHECK_EQ(clock_tap(&c, 1500000), 12000);
    CHECK_EQ(clock_tap(&c, 2000000), 12000);
    CHECK_EQ(clock_tap(&c, 2600000), 11250); /* mean of 500, 500, 600 ms */
    CHECK_EQ(clock_tap(&c, 6000000), 0);     /* timeout restarts */
    CHECK_EQ(clock_tap(&c, 6250000), 24000);
}

int main(void)
{
    test_internal();
    test_external();
    test_outputs();
    test_tap();
    return test_report("clock");
}
