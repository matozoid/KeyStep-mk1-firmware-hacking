#include "test.h"
#include "../src/core/keyscan.h"

static int n_events;
static uint8_t ev_key[16], ev_vel[16];

static void on_event(void *ctx, uint8_t key, uint8_t vel)
{
    (void)ctx;
    ev_key[n_events] = key;
    ev_vel[n_events] = vel;
    n_events++;
}

static void set_key(uint8_t rows[8], int key, int a, int b)
{
    int g = key >> 3, c = key & 7;
    rows[g * 2] = (uint8_t)((rows[g * 2] & ~(1 << c)) | (a << c));
    rows[g * 2 + 1] = (uint8_t)((rows[g * 2 + 1] & ~(1 << c)) | (b << c));
}

static void test_press_release(void)
{
    keyscan_t ks;
    uint8_t rows[8] = {0};
    keyscan_init(&ks);
    n_events = 0;

    set_key(rows, 13, 1, 0);
    keyscan_update(&ks, rows, 1000, on_event, 0);
    CHECK_EQ(n_events, 0);
    set_key(rows, 13, 1, 1);
    keyscan_update(&ks, rows, 1000 + 5000, on_event, 0);
    CHECK_EQ(n_events, 1);
    CHECK_EQ(ev_key[0], 13);
    CHECK(ev_vel[0] > 1 && ev_vel[0] < 127);

    set_key(rows, 13, 1, 0); /* B opens first: still held */
    keyscan_update(&ks, rows, 7000, on_event, 0);
    CHECK_EQ(n_events, 1);
    set_key(rows, 13, 0, 0);
    keyscan_update(&ks, rows, 8000, on_event, 0);
    CHECK_EQ(n_events, 2);
    CHECK_EQ(ev_vel[1], 0);
}

static void test_half_press(void)
{
    keyscan_t ks;
    uint8_t rows[8] = {0};
    keyscan_init(&ks);
    n_events = 0;
    set_key(rows, 31, 1, 0);
    keyscan_update(&ks, rows, 0, on_event, 0);
    set_key(rows, 31, 0, 0);
    keyscan_update(&ks, rows, 100, on_event, 0);
    CHECK_EQ(n_events, 0);
}

/* Contact A bouncing open during travel must not restart the travel timer. */
static void test_bounce_during_travel(void)
{
    keyscan_t ks;
    uint8_t rows[8] = {0};
    keyscan_init(&ks);
    n_events = 0;
    set_key(rows, 5, 1, 0);
    keyscan_update(&ks, rows, 1000, on_event, 0);
    set_key(rows, 5, 0, 0);
    keyscan_update(&ks, rows, 1300, on_event, 0);
    set_key(rows, 5, 1, 0);
    keyscan_update(&ks, rows, 1600, on_event, 0);
    set_key(rows, 5, 1, 1);
    keyscan_update(&ks, rows, 21000, on_event, 0);
    CHECK_EQ(n_events, 1);
    CHECK_EQ(ev_vel[0], keyscan_velocity(KEYSCAN_CURVE_LIN, 20000));

    /* a gap longer than the bounce window is a released half press */
    set_key(rows, 5, 0, 0);
    keyscan_update(&ks, rows, 30000, on_event, 0);
    CHECK_EQ(n_events, 2);
    set_key(rows, 5, 1, 0);
    keyscan_update(&ks, rows, 40000, on_event, 0);
    set_key(rows, 5, 0, 0);
    keyscan_update(&ks, rows, 41000, on_event, 0);
    keyscan_update(&ks, rows, 45000, on_event, 0);
    set_key(rows, 5, 1, 1);
    keyscan_update(&ks, rows, 46000, on_event, 0);
    CHECK_EQ(n_events, 3);
    CHECK_EQ(ev_vel[2], 127);
}

static void test_velocity_curves(void)
{
    for (int c = 0; c < 3; c++) {
        uint8_t prev = 127;
        for (uint32_t t = 500; t < 100000; t += 500) {
            uint8_t v = keyscan_velocity((uint8_t)c, t);
            CHECK(v >= 1 && v <= 127);
            CHECK(v <= prev); /* slower never louder */
            prev = v;
        }
    }
    CHECK_EQ(keyscan_velocity(0, 1000), 127);
    CHECK_EQ(keyscan_velocity(0, 400000), 1);
    CHECK_EQ(keyscan_velocity(0, 10000000), 1);
    /* medium stroke: Log louder than Lin, AntiLog softer */
    uint8_t lin = keyscan_velocity(KEYSCAN_CURVE_LIN, 20000);
    CHECK(lin > 40 && lin < 90);
    CHECK(keyscan_velocity(KEYSCAN_CURVE_LOG, 20000) > lin + 10);
    CHECK(keyscan_velocity(KEYSCAN_CURVE_ANTILOG, 20000) + 10 < lin);
}

int main(void)
{
    test_press_release();
    test_half_press();
    test_bounce_during_travel();
    test_velocity_curves();
    return test_report("keyscan");
}
