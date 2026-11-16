/* Master clock at 480 PPQN (Docs/re/sequencer.md section 4).
 *
 * Sources: the internal tempo, MIDI/USB clock (0xF8 = 20 ticks), or Sync In pulses. External
 * pulses are interpolated: the interval between the last two pulses is spread over the ticks of one
 * pulse, and any ticks still missing when the next pulse arrives are caught up at once, so the
 * internal 480 PPQN keeps running between pulses and never drifts.
 *
 * Hardware independent: the caller passes a microsecond time stamp and runs every returned tick
 * through the sequencer and clock_output(). */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#define CLOCK_PPQN          480
#define CLOCK_TICKS_PER_F8  20
#define CLOCK_BPM100_MIN    3000
#define CLOCK_BPM100_MAX    24000

enum clock_source { CLOCK_INTERNAL = 0, CLOCK_MIDI = 1, CLOCK_SYNC_IN = 2 };

enum sync_out_type { SYNC_OUT_1STEP_GATE, SYNC_OUT_1STEP_CLOCK, SYNC_OUT_KORG, SYNC_OUT_24PPQ, SYNC_OUT_48PPQ };

/* clock_output() flags */
#define CLOCK_OUT_MIDI_CLOCK 0x01 /* send 0xF8 */
#define CLOCK_OUT_SYNC_HIGH  0x02
#define CLOCK_OUT_SYNC_LOW   0x04
#define CLOCK_OUT_BEAT_ON    0x08
#define CLOCK_OUT_BEAT_OFF   0x10

#define TAP_MAX 4

typedef struct {
    uint16_t bpm100;
    uint8_t source;
    uint8_t sync_out;        /* enum sync_out_type */

    /* internal */
    uint32_t last_us;
    uint64_t acc;            /* tick phase accumulator */

    /* external */
    uint16_t ext_ticks_per_pulse;
    uint16_t ext_done;       /* ticks emitted since the last pulse */
    uint32_t ext_last_pulse_us;
    uint32_t ext_period_us;  /* 0 until two pulses have been seen */
    uint8_t ext_seen;        /* a pulse has been seen since reset */

    /* outputs */
    uint32_t tick;           /* ticks since start */

    /* tap tempo */
    uint32_t tap_us[TAP_MAX];
    uint8_t tap_n;
    uint8_t tap_avg;         /* intervals averaged, 1..3 */
} mclock_t;

void clock_init(mclock_t *c);
void clock_set_bpm100(mclock_t *c, uint32_t bpm100);
void clock_set_source(mclock_t *c, uint8_t source, uint16_t ticks_per_pulse);
void clock_reset(mclock_t *c, uint32_t now_us); /* transport start: position 0 */

/* Internal source (or interpolation between external pulses): ticks due at now_us. */
unsigned clock_poll(mclock_t *c, uint32_t now_us);

/* External pulse (0xF8 or a Sync In edge): ticks to run now (catch-up plus the pulse's own). */
unsigned clock_ext_pulse(mclock_t *c, uint32_t now_us);

/* Call once per tick, after advancing the sequencer; returns CLOCK_OUT_* flags. */
unsigned clock_output(mclock_t *c);

/* Tap tempo: returns the new bpm100, or 0 if this tap only started a measurement. */
uint32_t clock_tap(mclock_t *c, uint32_t now_us);

/* Sync Out ticks per pulse for a sync_out_type. */
uint16_t clock_sync_ticks(uint8_t type);

#endif
