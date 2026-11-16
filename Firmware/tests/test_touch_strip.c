#include "test.h"
#include "../src/core/touch_strip.h"

static const touch_cal_t cal = {0x09, 0xBF};

static void test_scale_map(void)
{
    CHECK_EQ(touch_scale(0, cal), TOUCH_NONE);
    CHECK_EQ(touch_scale(0x09, cal), 0);
    CHECK_EQ(touch_scale(0xBF, cal), 127);
    CHECK_EQ(touch_scale(0xFF, cal), 127);
    CHECK_EQ(touch_scale(1, cal), 0);
    CHECK_EQ(touch_pitch_map(0), 0);
    CHECK_EQ(touch_pitch_map(54), 64);
    CHECK_EQ(touch_pitch_map(60), 64);
    CHECK_EQ(touch_pitch_map(73), 64);
    CHECK_EQ(touch_pitch_map(127), 127);
    uint8_t prev = 0;
    for (int s = 0; s < 128; s++) {
        uint8_t v = touch_pitch_map((uint8_t)s);
        CHECK(v >= prev && v <= 127);
        prev = v;
    }
}

static void test_spring(void)
{
    pitch_strip_t p;
    pitch_strip_init(&p);
    CHECK_EQ(pitch_strip_update(&p, 0, cal), -1);
    CHECK_EQ(pitch_strip_update(&p, 0x09, cal), 0); /* full down */
    CHECK_EQ(pitch_strip_update(&p, 0x09, cal), -1);
    int steps = 0, v, prev = 0;
    while ((v = pitch_strip_update(&p, 0, cal)) >= 0) {
        CHECK(v >= prev);
        prev = v;
        steps++;
    }
    CHECK_EQ(prev, 64);
    CHECK_EQ(steps, 17);

    CHECK_EQ(pitch_strip_update(&p, 0xBF, cal), 127); /* full up */
    steps = 0;
    prev = 127;
    while ((v = pitch_strip_update(&p, 0, cal)) >= 0) {
        CHECK(v <= prev);
        prev = v;
        steps++;
    }
    CHECK_EQ(prev, 64);
}

static void test_mod(void)
{
    mod_strip_t m;
    mod_strip_init(&m);
    CHECK_EQ(mod_strip_update(&m, 0, cal), -1);
    CHECK_EQ(mod_strip_update(&m, 0xBF, cal), 127);
    CHECK_EQ(mod_strip_update(&m, 0xBF, cal), -1);
    CHECK_EQ(mod_strip_update(&m, 0, cal), -1); /* no return to 0 */
}

int main(void)
{
    test_scale_map();
    test_spring();
    test_mod();
    return test_report("touch_strip");
}
