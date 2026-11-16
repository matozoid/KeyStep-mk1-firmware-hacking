/* Sequence data and editing (step record, real-time record, tap, clear).
 *
 * The in-memory layout is the stock one (Docs/re/sequencer.md section 1), so sequences saved by the
 * stock firmware in flash keep working after switching firmware, and the other way round. */
#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <stdint.h>

#define SEQ_STEPS        64
#define SEQ_STEP_NOTES   8
#define SEQ_COUNT        8

#define SEQ_NOTE_TIE     0x81
#define SEQ_NOTE_REST    0x82
#define SEQ_NOTE_EMPTY   0xFF
#define SEQ_VEL_TIE_FLAG 0x80 /* on slot 0's velocity: step tie/legato flag */

#define SEQ_FLAG_PLACEHOLDER 0x01
#define SEQ_FLAG_DIRTY       0x02

typedef struct {
    uint8_t note;
    uint8_t vel;
} seq_slot_t;

typedef struct {
    seq_slot_t step[SEQ_STEPS][SEQ_STEP_NOTES];
    uint8_t length;   /* 1..64 (0 while recording into an empty sequence) */
    uint8_t swing;    /* 50..75 % */
    uint8_t gate;     /* 1..99 % */
    uint8_t flags;    /* SEQ_FLAG_* */
    uint8_t tie_conv; /* 1: tie flag on the step that starts the tie, 0: on the step it continues into */
    uint8_t pad[3];
} sequence_t;

_Static_assert(sizeof(sequence_t) == 0x408, "stock sequence layout");

/* Step helpers */
int seq_step_is_rest(const sequence_t *s, unsigned step);
int seq_step_tied(const sequence_t *s, unsigned step); /* tie flag on slot 0 */
unsigned seq_step_notes(const sequence_t *s, unsigned step, uint8_t notes[SEQ_STEP_NOTES],
                        uint8_t vels[SEQ_STEP_NOTES]);

/* Editing (all mark the sequence dirty) */
void seq_init_empty(sequence_t *s, uint8_t tie_conv);  /* placeholder: length 1, one rest step */
void seq_clear_steps(sequence_t *s);                   /* all steps rest/100, length kept */
void seq_set_step(sequence_t *s, unsigned step, const uint8_t *notes, const uint8_t *vels, unsigned n, int tie);
void seq_set_rest(sequence_t *s, unsigned step, uint8_t vel);
void seq_set_length(sequence_t *s, unsigned len, int overwrite_new);
void seq_clear_last(sequence_t *s);
void seq_set_swing(sequence_t *s, uint8_t swing);
void seq_set_gate(sequence_t *s, uint8_t gate);

/* ---- Step recorder (transport stopped) ---- */
typedef struct {
    sequence_t *seq;
    uint8_t notes[SEQ_STEP_NOTES], vels[SEQ_STEP_NOTES];
    uint8_t n;          /* notes collected for the pending step */
    uint8_t held;       /* keys currently down */
    uint8_t started;    /* first input since arming seen */
    uint8_t tap_held;
    uint8_t tap_mark;     /* Tap went down and no step was written since */
    uint8_t rest_pending; /* Tap went down with no key held */
    uint8_t tie_written;  /* Tap released with keys held: chord already written with tie */
    uint8_t tie_conv_default;
} step_rec_t;

void step_rec_arm(step_rec_t *r, sequence_t *seq, uint8_t tie_conv_default);
void step_rec_start(step_rec_t *r); /* first input after arming: clear the sequence */
void step_rec_note_on(step_rec_t *r, uint8_t note, uint8_t vel);
void step_rec_note_off(step_rec_t *r, uint8_t note);
void step_rec_tap(step_rec_t *r, int pressed);
void step_rec_delete_last(step_rec_t *r);

/* ---- Real-time recorder (while playing): replace the step under the play head ---- */
typedef struct {
    uint8_t notes[SEQ_STEP_NOTES], vels[SEQ_STEP_NOTES];
    uint8_t n;
    uint8_t last_step_written;
    uint8_t have_last;
    uint8_t tap;          /* tap pressed since the last step */
} rt_rec_t;

void rt_rec_reset(rt_rec_t *r);
void rt_rec_note_on(rt_rec_t *r, uint8_t note, uint8_t vel);
void rt_rec_note_off(rt_rec_t *r, uint8_t note);
void rt_rec_tap(rt_rec_t *r);
/* Called when the quantised record position reaches a new step. */
void rt_rec_write(rt_rec_t *r, sequence_t *s, unsigned step);

#endif
