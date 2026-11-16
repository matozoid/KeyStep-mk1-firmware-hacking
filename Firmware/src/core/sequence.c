#include "sequence.h"

static void mark(sequence_t *s)
{
    s->flags |= SEQ_FLAG_DIRTY;
}

int seq_step_is_rest(const sequence_t *s, unsigned step)
{
    for (unsigned i = 0; i < SEQ_STEP_NOTES; i++) {
        uint8_t n = s->step[step][i].note;
        if (n < 0x80)
            return 0;
        if (n == SEQ_NOTE_EMPTY)
            break;
    }
    return 1;
}

int seq_step_tied(const sequence_t *s, unsigned step)
{
    return (s->step[step][0].vel & SEQ_VEL_TIE_FLAG) != 0 || s->step[step][0].note == SEQ_NOTE_TIE;
}

unsigned seq_step_notes(const sequence_t *s, unsigned step, uint8_t notes[SEQ_STEP_NOTES],
                        uint8_t vels[SEQ_STEP_NOTES])
{
    unsigned n = 0;
    for (unsigned i = 0; i < SEQ_STEP_NOTES; i++) {
        const seq_slot_t *sl = &s->step[step][i];
        if (sl->note == SEQ_NOTE_EMPTY)
            break;
        if (sl->note < 0x80) {
            notes[n] = sl->note;
            vels[n] = sl->vel & 0x7F;
            n++;
        }
    }
    return n;
}

void seq_set_rest(sequence_t *s, unsigned step, uint8_t vel)
{
    s->step[step][0].note = SEQ_NOTE_REST;
    s->step[step][0].vel = vel;
    for (unsigned i = 1; i < SEQ_STEP_NOTES; i++) {
        s->step[step][i].note = SEQ_NOTE_EMPTY;
        s->step[step][i].vel = 0x40;
    }
    mark(s);
}

void seq_clear_steps(sequence_t *s)
{
    for (unsigned st = 0; st < SEQ_STEPS; st++)
        seq_set_rest(s, st, 100);
}

void seq_init_empty(sequence_t *s, uint8_t tie_conv)
{
    seq_clear_steps(s);
    s->length = 1;
    s->swing = 50;
    s->gate = 50;
    s->tie_conv = tie_conv;
    s->pad[0] = s->pad[1] = s->pad[2] = 0;
    s->flags = SEQ_FLAG_PLACEHOLDER | SEQ_FLAG_DIRTY;
}

void seq_set_step(sequence_t *s, unsigned step, const uint8_t *notes, const uint8_t *vels, unsigned n, int tie)
{
    if (step >= SEQ_STEPS)
        return;
    if (n == 0) {
        seq_set_rest(s, step, 0x40);
    } else {
        for (unsigned i = 0; i < SEQ_STEP_NOTES; i++) {
            s->step[step][i].note = i < n ? notes[i] : SEQ_NOTE_EMPTY;
            s->step[step][i].vel = i < n ? (uint8_t)(vels[i] & 0x7F) : 0x40;
        }
    }
    if (tie)
        s->step[step][0].vel |= SEQ_VEL_TIE_FLAG;
    mark(s);
}

void seq_set_length(sequence_t *s, unsigned len, int overwrite_new)
{
    if (len < 1)
        len = 1;
    if (len > SEQ_STEPS)
        len = SEQ_STEPS;
    for (unsigned st = s->length; st < len; st++) {
        if (overwrite_new || s->step[st][0].note >= 0x80)
            seq_set_rest(s, st, 100);
    }
    s->length = (uint8_t)len;
    s->flags &= (uint8_t)~SEQ_FLAG_PLACEHOLDER;
    mark(s);
}

void seq_clear_last(sequence_t *s)
{
    if (s->length < 2) {
        seq_set_rest(s, 0, 0x40);
        s->length = 1;
        s->flags |= SEQ_FLAG_PLACEHOLDER;
    } else {
        s->length--;
    }
    mark(s);
}

void seq_set_swing(sequence_t *s, uint8_t swing)
{
    s->swing = swing < 50 ? 50 : swing > 75 ? 75 : swing;
    mark(s);
}

void seq_set_gate(sequence_t *s, uint8_t gate)
{
    s->gate = gate < 1 ? 1 : gate > 99 ? 99 : gate;
    mark(s);
}

/* ---------------- step record ---------------- */

void step_rec_arm(step_rec_t *r, sequence_t *seq, uint8_t tie_conv_default)
{
    r->seq = seq;
    r->n = 0;
    r->held = 0;
    r->started = 0;
    r->tap_held = 0;
    r->tap_mark = 0;
    r->rest_pending = 0;
    r->tie_written = 0;
    r->tie_conv_default = tie_conv_default;
}

static void step_rec_begin(step_rec_t *r)
{
    if (r->started)
        return;
    r->started = 1;
    sequence_t *s = r->seq;
    /* First input after arming starts a fresh recording. */
    seq_clear_steps(s);
    s->length = 0;
    s->tie_conv = r->tie_conv_default;
    s->flags &= (uint8_t)~SEQ_FLAG_PLACEHOLDER;
}

void step_rec_note_on(step_rec_t *r, uint8_t note, uint8_t vel)
{
    step_rec_begin(r);
    r->held++;
    for (unsigned i = 0; i < r->n; i++)
        if (r->notes[i] == note)
            return;
    if (r->n < SEQ_STEP_NOTES) {
        r->notes[r->n] = note;
        r->vels[r->n] = vel;
        r->n++;
    }
}

/* Write the collected chord to the next step (stock 0x080151c0 / 0x08015794). With `tie` the step
 * carries the tie bit and the step after it is preset to a rest. */
static void step_rec_write(step_rec_t *r, int tie)
{
    sequence_t *s = r->seq;
    if (s->length >= SEQ_STEPS)
        return;
    seq_set_step(s, s->length, r->notes, r->vels, r->n, tie);
    s->length++;
    if (tie && s->length < SEQ_STEPS)
        seq_set_rest(s, s->length, 100);
    r->tap_mark = 0;
}

void step_rec_note_off(step_rec_t *r, uint8_t note)
{
    (void)note;
    if (r->held)
        r->held--;
    if (r->held == 0 && r->n) {
        if (!r->tie_written) {
            /* Tap held while the keys go up: legato entry, the step gets the tie bit. */
            if (r->tap_held)
                r->rest_pending = 0;
            step_rec_write(r, r->tap_held);
        } else {
            r->rest_pending = 0;
            r->seq->flags |= SEQ_FLAG_DIRTY;
        }
        r->n = 0;
        r->tie_written = 0;
    }
}

/* Tap in step record, as stock 0x08015794.
 *  - No key held: a rest is written when Tap is released, unless a chord was entered meanwhile.
 *  - Keys held, Tie to Previous (convention 0): each Tap release writes the chord with the tie
 *    bit; releasing the keys adds nothing. N taps give N steps.
 *  - Keys held, Tie to Next (convention 1): each Tap press writes the chord with the tie bit;
 *    releasing the keys adds the last step without it. N taps give N + 1 steps. */
void step_rec_tap(step_rec_t *r, int pressed)
{
    step_rec_begin(r);
    sequence_t *s = r->seq;
    if (pressed) {
        r->tap_held = 1;
        r->tap_mark = s->length < SEQ_STEPS;
        if (!r->held || !r->n) {
            r->rest_pending = 1;
        } else if (s->tie_conv == 1 && s->length < SEQ_STEPS) {
            r->rest_pending = 0;
            step_rec_write(r, 1);
        }
        return;
    }
    if (r->rest_pending && s->length < SEQ_STEPS) {
        r->rest_pending = 0;
        seq_set_rest(s, s->length, 0x40);
        s->length++;
    } else if (r->tap_mark && r->n && s->length < SEQ_STEPS) {
        seq_set_step(s, s->length, r->notes, r->vels, r->n, 1);
        s->length++;
        r->tie_written = 1;
    }
    r->tap_mark = 0;
    r->tap_held = 0;
}

void step_rec_delete_last(step_rec_t *r)
{
    seq_clear_last(r->seq);
    r->n = 0;
}

/* ---------------- real-time record ---------------- */

void rt_rec_reset(rt_rec_t *r)
{
    r->n = 0;
    r->have_last = 0;
    r->tap = 0;
}

void rt_rec_note_on(rt_rec_t *r, uint8_t note, uint8_t vel)
{
    for (unsigned i = 0; i < r->n; i++)
        if (r->notes[i] == note)
            return;
    if (r->n < SEQ_STEP_NOTES) {
        r->notes[r->n] = note;
        r->vels[r->n] = vel;
        r->n++;
    }
    r->have_last = 0; /* new attack */
}

void rt_rec_note_off(rt_rec_t *r, uint8_t note)
{
    for (unsigned i = 0; i < r->n; i++) {
        if (r->notes[i] == note) {
            for (; i + 1 < r->n; i++) {
                r->notes[i] = r->notes[i + 1];
                r->vels[i] = r->vels[i + 1];
            }
            r->n--;
            return;
        }
    }
}

void rt_rec_tap(rt_rec_t *r)
{
    r->tap = 1;
}

void rt_rec_write(rt_rec_t *r, sequence_t *s, unsigned step)
{
    if (step >= SEQ_STEPS)
        return;
    if (r->n) {
        /* Same chord still held from the previous written step: tie instead of re-attack. */
        int tie_on = r->have_last;
        if (tie_on && s->tie_conv == 1 && r->last_step_written < SEQ_STEPS)
            s->step[r->last_step_written][0].vel |= SEQ_VEL_TIE_FLAG;
        seq_set_step(s, step, r->notes, r->vels, r->n, tie_on && s->tie_conv == 0);
        r->have_last = 1;
        r->last_step_written = (uint8_t)step;
    } else if (r->tap) {
        seq_set_rest(s, step, 0x40);
        r->have_last = 0;
    } else {
        r->have_last = 0;
    }
    r->tap = 0;
}
