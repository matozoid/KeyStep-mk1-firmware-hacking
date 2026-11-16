#include "keyscan.h"

enum { KEY_UP, KEY_TRAVEL, KEY_TRAVEL_GAP, KEY_DOWN };

/* Contact A may bounce open for a moment on the way down. The travel timer keeps running
 * through gaps shorter than this; a longer gap is a released half press. */
#define BOUNCE_GAP_US 3000u

/* Velocity from key travel time (our own curves; the stock firmware uses its own breakpoint
 * tables, keyboard.md section 2). The travel time is placed on a log2 scale between 1 ms
 * (velocity 127) and 400 ms (velocity 1), giving a strength s = 0..255. Lin maps s straight
 * to velocity, Log eases out (louder for medium strokes), AntiLog eases in (softer). */
#define T_FAST_US 1000u
#define T_SLOW_US 400000u

/* log2(x) in 1/16 steps, x >= 1: integer part plus a linear mantissa */
static unsigned log2_q4(uint32_t x)
{
    unsigned e = 0;
    while ((x >> e) > 1u)
        e++;
    uint32_t frac = e >= 4 ? (x >> (e - 4)) & 15u : (x << (4 - e)) & 15u;
    return e * 16u + (unsigned)frac;
}

void keyscan_init(keyscan_t *ks)
{
    for (int k = 0; k < KEYSCAN_KEYS; k++) {
        ks->state[k] = KEY_UP;
        ks->t_first[k] = 0;
    }
    ks->curve = KEYSCAN_CURVE_LIN;
}

uint8_t keyscan_velocity(uint8_t curve, uint32_t travel_us)
{
    static unsigned l_fast, l_slow;
    if (!l_slow) {
        l_fast = log2_q4(T_FAST_US);
        l_slow = log2_q4(T_SLOW_US);
    }
    if (travel_us <= T_FAST_US)
        return 127;
    if (travel_us >= T_SLOW_US)
        return 1;
    unsigned l = log2_q4(travel_us);
    unsigned s = l >= l_slow ? 0 : (l_slow - l) * 255u / (l_slow - l_fast); /* 0..255 */
    if (s > 255u)
        s = 255u;
    if (curve == KEYSCAN_CURVE_LOG)
        s = 255u - (255u - s) * (255u - s) / 255u;
    else if (curve == KEYSCAN_CURVE_ANTILOG)
        s = s * s / 255u;
    return (uint8_t)(1u + s * 126u / 255u);
}

void keyscan_update(keyscan_t *ks, const uint8_t rows[8], uint32_t now_us, keyscan_event_fn fn, void *ctx)
{
    for (int k = 0; k < KEYSCAN_KEYS; k++) {
        int group = k >> 3, col = k & 7;
        int a = (rows[group * 2] >> col) & 1;
        int b = (rows[group * 2 + 1] >> col) & 1;

        switch (ks->state[k]) {
        case KEY_UP:
            if (a && b) { /* both closed within one scan: maximum speed */
                ks->state[k] = KEY_DOWN;
                fn(ctx, (uint8_t)k, 127);
            } else if (a) {
                ks->state[k] = KEY_TRAVEL;
                ks->t_first[k] = now_us;
            }
            break;
        case KEY_TRAVEL:
            if (b) {
                ks->state[k] = KEY_DOWN;
                fn(ctx, (uint8_t)k, keyscan_velocity(ks->curve, now_us - ks->t_first[k]));
            } else if (!a) {
                ks->state[k] = KEY_TRAVEL_GAP;
                ks->t_open[k] = now_us;
            }
            break;
        case KEY_TRAVEL_GAP:
            if (b) {
                ks->state[k] = KEY_DOWN;
                fn(ctx, (uint8_t)k, keyscan_velocity(ks->curve, now_us - ks->t_first[k]));
            } else if (a) {
                ks->state[k] = KEY_TRAVEL;
            } else if (now_us - ks->t_open[k] >= BOUNCE_GAP_US) {
                ks->state[k] = KEY_UP; /* half press released */
            }
            break;
        default: /* KEY_DOWN: note off once the first contact opens */
            if (!a && !b) {
                ks->state[k] = KEY_UP;
                fn(ctx, (uint8_t)k, 0);
            }
            break;
        }
    }
}
