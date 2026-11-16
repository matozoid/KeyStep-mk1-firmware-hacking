#include "test.h"
#include "../src/core/led_level.h"

static void test_boot_full(void)
{
    led_level_t l;
    led_level_init(&l, 1, 100); /* full = 90 %, ramps 0 -> 90 over 900 ms */
    CHECK_EQ(led_level_tick(&l, 1, 100), 1);
    uint8_t p = 0;
    for (int i = 1; i < 450; i++)
        p = led_level_tick(&l, 1, 100);
    CHECK(p >= 44 && p <= 46);
    for (int i = 0; i < 450; i++)
        p = led_level_tick(&l, 1, 100);
    CHECK_EQ(p, 90);
    CHECK_EQ(l.mode, 0);
}

static void test_boot_low(void)
{
    led_level_t l;
    led_level_init(&l, 0, 100); /* low = 20 %, 20 ms per percent */
    uint8_t p = 0;
    for (int i = 0; i < 200; i++)
        p = led_level_tick(&l, 0, 100);
    CHECK_EQ(p, 10);
    for (int i = 0; i < 200; i++)
        p = led_level_tick(&l, 0, 100);
    CHECK_EQ(p, 20);
}

static void test_fade(void)
{
    led_level_t l;
    led_level_init(&l, 1, 50); /* full 45, low 10 */
    for (int i = 0; i < 1000; i++)
        led_level_tick(&l, 1, 50);
    CHECK_EQ(led_level_tick(&l, 1, 50), 45);
    /* PD4 drops: fade 45 -> 10 over 350 ms */
    uint8_t p = led_level_tick(&l, 0, 50);
    CHECK_EQ(p, 44);
    for (int i = 1; i < 175; i++)
        p = led_level_tick(&l, 0, 50);
    CHECK(p >= 27 && p <= 28);
    for (int i = 0; i < 175; i++)
        p = led_level_tick(&l, 0, 50);
    CHECK_EQ(p, 10);
    /* back up */
    for (int i = 0; i < 350; i++)
        p = led_level_tick(&l, 1, 50);
    CHECK_EQ(p, 45);
    /* setting change while idle applies at once, never fully dark */
    CHECK_EQ(led_level_tick(&l, 1, 100), 90);
    for (int i = 0; i < 1000; i++)
        p = led_level_tick(&l, 0, 4);
    CHECK_EQ(p, 1);
}

int main(void)
{
    test_boot_full();
    test_boot_low();
    test_fade();
    return test_report("led_level");
}
