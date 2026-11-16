#include "seq_player.h"

static const uint16_t division_ticks[8] = {480, 240, 120, 60, 320, 160, 80, 40};

uint16_t seq_division_ticks(unsigned index)
{
    return division_ticks[index < 8 ? index : 2];
}

static unsigned seq_len(const seq_player_t *p)
{
    unsigned len = p->seq ? p->seq->length : 0;
    return len ? len : 1;
}

static uint8_t fold(int n)
{
    while (n > 127)
        n -= 12;
    while (n < 0)
        n += 12;
    return (uint8_t)n;
}

static void all_off(seq_player_t *p)
{
    for (unsigned i = 0; i < p->n_sounding; i++)
        p->fn(p->ctx, 0, p->sounding[i], 0x40);
    p->n_sounding = 0;
    p->tie_pending = 0;
}

static int sounding_has(const uint8_t *list, unsigned n, uint8_t note)
{
    for (unsigned i = 0; i < n; i++)
        if (list[i] == note)
            return 1;
    return 0;
}

/* Does the note-off of step k get held because the next step continues it? */
static int step_tied_forward(const seq_player_t *p, unsigned k)
{
    const sequence_t *s = p->seq;
    unsigned next = (k + 1) % seq_len(p);
    if (s->step[next][0].note == SEQ_NOTE_TIE)
        return 1;
    if (seq_step_is_rest(s, next))
        return 0;
    if (s->tie_conv == 1)
        return (s->step[k][0].vel & SEQ_VEL_TIE_FLAG) != 0;
    return (s->step[next][0].vel & SEQ_VEL_TIE_FLAG) != 0;
}

static void step_off(seq_player_t *p)
{
    p->off_countdown = -1;
    if (p->n_sounding && step_tied_forward(p, p->off_step)) {
        p->tie_pending = 1;
        return;
    }
    all_off(p);
}

static void step_on(seq_player_t *p, unsigned k)
{
    uint8_t notes[SEQ_STEP_NOTES], in_vels[SEQ_STEP_NOTES];
    uint8_t out[SEQ_MAX_SOUNDING], vels[SEQ_MAX_SOUNDING];
    unsigned m = seq_step_notes(p->seq, k, notes, in_vels);
    unsigned n = 0;

    if (m == 0 || p->mute) {
        all_off(p);
        p->tie_pending = 0;
        return;
    }
    for (unsigned i = 0; i < m; i++) {
        int root = notes[i] + p->octave_shift;
        unsigned extra = p->chord && p->chord_n ? p->chord_n : 1;
        for (unsigned c = 0; c < extra && n < SEQ_MAX_SOUNDING; c++) {
            int interval = c ? p->chord[c] - p->chord[0] : 0;
            uint8_t o = fold(root + interval + p->transpose);
            if (sounding_has(out, n, o))
                continue;
            out[n] = o;
            vels[n] = in_vels[i];
            n++;
        }
    }

    if (!p->tie_pending) {
        all_off(p);
        for (unsigned i = 0; i < n; i++)
            p->fn(p->ctx, 1, out[i], p->fixed_vel ? p->fixed_vel : vels[i]);
    } else {
        /* legato: new notes first, then release the old ones that are not continued */
        for (unsigned i = 0; i < n; i++)
            if (!sounding_has(p->sounding, p->n_sounding, out[i]))
                p->fn(p->ctx, 1, out[i], p->fixed_vel ? p->fixed_vel : vels[i]);
        for (unsigned i = 0; i < p->n_sounding; i++)
            if (!sounding_has(out, n, p->sounding[i]))
                p->fn(p->ctx, 0, p->sounding[i], 0x40);
    }
    for (unsigned i = 0; i < n; i++)
        p->sounding[i] = out[i];
    p->n_sounding = (uint8_t)n;
    p->tie_pending = 0;
}

/* Note-on delay from the grid start and gate length for the current step. */
static void step_timing(const seq_player_t *p, uint32_t *delay, uint32_t *gate_len)
{
    uint32_t T = p->T, s = p->seq->swing, g = p->seq->gate;
    if (s <= 50) {
        uint32_t gl = (T - 1) * g / 100;
        if (gl < 15)
            gl = 15;
        if (gl > T - 1)
            gl = T - 1;
        *delay = 0;
        *gate_len = gl;
        return;
    }
    uint32_t pair_len = 2 * T * s / 100;
    if (p->phase == 0) {
        *delay = 0;
        *gate_len = pair_len * g / 100;
    } else {
        *delay = pair_len - T;
        *gate_len = (T - *delay) * g / 100;
    }
    if (*gate_len == 0)
        *gate_len = 1;
}

static void reset_position(seq_player_t *p)
{
    p->step = 0;
    p->tick_in_step = 0;
    p->phase = 0;
    p->started_step = 0;
    p->global_tick = 0;
}

void seq_player_init(seq_player_t *p, seq_note_fn fn, void *ctx)
{
    *p = (seq_player_t){0};
    p->fn = fn;
    p->ctx = ctx;
    p->T = 120;
    p->off_countdown = -1;
}

void seq_player_start(seq_player_t *p, const sequence_t *s)
{
    all_off(p);
    if (s)
        p->seq = s;
    p->queued = 0;
    reset_position(p);
    p->off_countdown = -1;
    p->mute = 0;
    p->running = p->seq != 0;
}

void seq_player_continue(seq_player_t *p)
{
    if (p->seq)
        p->running = 1;
}

void seq_player_pause(seq_player_t *p)
{
    p->running = 0;
    p->off_countdown = -1;
    p->started_step = 0; /* replay the step on continue */
    p->tick_in_step = 0;
    all_off(p);
}

void seq_player_stop(seq_player_t *p)
{
    p->running = 0;
    p->mute = 0;
    p->off_countdown = -1;
    all_off(p);
    reset_position(p);
}

void seq_player_set_mute(seq_player_t *p, int on)
{
    p->mute = on != 0;
    if (on) {
        p->off_countdown = -1;
        p->tie_pending = 0;
        all_off(p);
    }
}

void seq_player_restart(seq_player_t *p)
{
    all_off(p);
    p->off_countdown = -1;
    reset_position(p);
}

void seq_player_set_division(seq_player_t *p, unsigned index)
{
    uint16_t t = seq_division_ticks(index);
    if (!p->running) {
        p->T = t;
        p->queued_T = 0;
    } else if (t != p->T) {
        p->queued_T = t;
    }
}

void seq_player_select(seq_player_t *p, const sequence_t *s)
{
    if (!p->running || !p->seq) {
        p->seq = s;
        p->step = 0;
        return;
    }
    p->queued = s;
}

static void apply_queued_seq(seq_player_t *p, int at_wrap)
{
    if (!p->queued)
        return;
    if (p->next_mode == SEQ_NEXT_END && !at_wrap)
        return;
    p->seq = p->queued;
    p->queued = 0;
    if (p->next_mode == SEQ_NEXT_INSTANT_CONTINUE) {
        p->step = (uint8_t)((p->global_tick / p->T) % seq_len(p));
    } else {
        p->step = 0;
        p->phase = 0;
    }
}

static void advance_step(seq_player_t *p)
{
    p->tick_in_step = 0;
    p->started_step = 0;
    p->phase ^= 1;
    p->step++;
    int wrapped = 0;
    if (p->step >= seq_len(p)) {
        p->step = 0;
        wrapped = 1;
    }
    apply_queued_seq(p, wrapped);
    if (p->hook) {
        p->step = p->hook(p->hook_ctx, p->step, wrapped);
        if (p->step >= seq_len(p))
            p->step = 0;
    }
}

void seq_player_tick(seq_player_t *p)
{
    if (!p->running || !p->seq)
        return;

    /* division change on the new grid */
    if (p->queued_T && p->global_tick % p->queued_T == 0) {
        if (p->tick_in_step)
            advance_step(p);
        p->T = p->queued_T;
        p->queued_T = 0;
    }

    if (p->off_countdown == 0)
        step_off(p);

    uint32_t delay, gate_len;
    step_timing(p, &delay, &gate_len);
    if (!p->started_step && p->tick_in_step >= delay) {
        if (p->off_countdown >= 0)
            step_off(p);
        step_on(p, p->step);
        p->started_step = 1;
        p->off_step = p->step;
        p->off_countdown = (int32_t)gate_len;
    }

    if (p->off_countdown > 0)
        p->off_countdown--;

    p->global_tick++;
    if (++p->tick_in_step >= p->T)
        advance_step(p);
}

unsigned seq_player_record_step(const seq_player_t *p)
{
    uint32_t pos = (uint32_t)p->step * p->T + p->tick_in_step;
    return ((pos + p->T / 2) / p->T) % seq_len(p);
}
