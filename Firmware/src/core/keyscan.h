/* Keybed contact state machine: raw two-contact matrix samples -> note on/off with velocity.
 * Hardware independent; the driver supplies the 8 select-row bytes and a microsecond time stamp. */
#ifndef KEYSCAN_H
#define KEYSCAN_H

#include <stdint.h>

#define KEYSCAN_KEYS 32

/* Stock curve indices. Which index the manual calls Lin / Log / Anti-log is unconfirmed (?). */
enum keyscan_curve { KEYSCAN_CURVE_LIN = 0, KEYSCAN_CURVE_LOG = 1, KEYSCAN_CURVE_ANTILOG = 2 };

typedef struct {
    uint8_t state[KEYSCAN_KEYS];
    uint32_t t_first[KEYSCAN_KEYS];
    uint32_t t_open[KEYSCAN_KEYS]; /* contact A opened during travel (bounce gap) */
    uint8_t curve;
} keyscan_t;

typedef void (*keyscan_event_fn)(void *ctx, uint8_t key, uint8_t velocity); /* velocity 0 = off */

void keyscan_init(keyscan_t *ks);

/* rows[r] = contacts closed on select row r (bit c = column c). Row 2g = A contacts and
 * row 2g+1 = B contacts of keys 8g..8g+7. */
void keyscan_update(keyscan_t *ks, const uint8_t rows[8], uint32_t now_us, keyscan_event_fn fn, void *ctx);

/* Travel time (A closed -> B closed) in microseconds -> MIDI velocity 1..127. */
uint8_t keyscan_velocity(uint8_t curve, uint32_t travel_us);

#endif
