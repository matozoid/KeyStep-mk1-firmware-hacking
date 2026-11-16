/* Sequence playback engine, driven by 480 PPQN clock ticks (Docs/re/sequencer.md section 2).
 * Hardware independent: note events go to a callback, the caller routes them to MIDI and CV. */
#ifndef SEQ_PLAYER_H
#define SEQ_PLAYER_H

#include <stdint.h>
#include "sequence.h"

#define SEQ_PPQN 480

enum seq_next_mode { SEQ_NEXT_END = 0, SEQ_NEXT_INSTANT_RESET = 1, SEQ_NEXT_INSTANT_CONTINUE = 2 };

/* on = 1: note on with velocity; on = 0: note off (velocity 0x40). gate follows "any note sounding". */
typedef void (*seq_note_fn)(void *ctx, int on, uint8_t note, uint8_t vel);

/* Optional hook called after the step advances (wrapped = 1 at the loop point). Returns the step to
 * play next; the arpeggiator uses it for pattern rebuilds, octave cycling and the random walk. */
typedef uint8_t (*seq_step_hook)(void *ctx, uint8_t step, int wrapped);

#define SEQ_MAX_SOUNDING 32
#define SEQ_CHORD_MAX    16

typedef struct {
    const sequence_t *seq;
    const sequence_t *queued;     /* pending sequence change (Next Seq) */
    seq_note_fn fn;
    void *ctx;

    uint16_t T;                   /* ticks per step */
    uint16_t queued_T;            /* pending division change, 0 = none */
    uint32_t global_tick;         /* ticks since start (division alignment, Instant Continue) */
    uint8_t step;                 /* current step index */
    uint16_t tick_in_step;        /* ticks since the grid start of the current step */
    uint8_t phase;                /* swing phase bit */
    uint8_t started_step;         /* note-on of the current step done */
    int32_t off_countdown;        /* ticks until the pending note-off, -1 = none */
    uint8_t off_step;             /* step whose note-off is pending */
    uint8_t tie_pending;          /* previous note-off was held because of a tie */
    uint8_t next_mode;            /* enum seq_next_mode */
    uint8_t running;
    uint8_t mute;                 /* keep the position running but play nothing */

    int8_t transpose;             /* semitones, folded into 0..127 by octaves */
    int8_t octave_shift;          /* extra semitones (arp octave), applied before transpose */
    uint8_t fixed_vel;            /* 0 = as recorded */

    seq_step_hook hook;
    void *hook_ctx;

    /* Chord expansion: each step note also plays note + (chord[i] - chord[0]), i >= 1. */
    const uint8_t *chord;
    uint8_t chord_n;

    uint8_t sounding[SEQ_MAX_SOUNDING];
    uint8_t n_sounding;
} seq_player_t;

/* Ticks per step for division index 0..7 (1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T, 1/32T). */
uint16_t seq_division_ticks(unsigned index);

void seq_player_init(seq_player_t *p, seq_note_fn fn, void *ctx);
void seq_player_start(seq_player_t *p, const sequence_t *s); /* from step 0 */
void seq_player_continue(seq_player_t *p);                   /* resume where paused */
void seq_player_pause(seq_player_t *p);                      /* all notes off, keep position */
void seq_player_stop(seq_player_t *p);                       /* all notes off, reset position */
void seq_player_set_mute(seq_player_t *p, int on);            /* cleared by start and stop */
void seq_player_restart(seq_player_t *p);                    /* resync to step 0 without stopping */
void seq_player_set_division(seq_player_t *p, unsigned index);
void seq_player_select(seq_player_t *p, const sequence_t *s); /* change sequence per next_mode */

/* Advance one 480 PPQN tick. */
void seq_player_tick(seq_player_t *p);

/* Step index currently playing (for the record position and LEDs). */
static inline unsigned seq_player_step(const seq_player_t *p) { return p->step; }

/* Real-time record quantisation: step index the current position rounds to. */
unsigned seq_player_record_step(const seq_player_t *p);

#endif
