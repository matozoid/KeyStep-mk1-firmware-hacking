#include "arp.h"

/* ---------------- note list ---------------- */

void note_list_clear(note_list_t *l)
{
    l->n = 0;
}

int note_list_has(const note_list_t *l, uint8_t note)
{
    for (unsigned i = 0; i < l->n; i++)
        if (l->note[i] == note)
            return 1;
    return 0;
}

int note_list_add(note_list_t *l, uint8_t note, uint8_t vel)
{
    if (note_list_has(l, note) || l->n >= ARP_MAX_NOTES)
        return 0;
    l->note[l->n] = note;
    l->vel[l->n] = vel;
    l->n++;
    return 1;
}

int note_list_remove(note_list_t *l, uint8_t note)
{
    for (unsigned i = 0; i < l->n; i++) {
        if (l->note[i] == note) {
            for (; i + 1 < l->n; i++) {
                l->note[i] = l->note[i + 1];
                l->vel[i] = l->vel[i + 1];
            }
            l->n--;
            return 1;
        }
    }
    return 0;
}

/* ---------------- helpers ---------------- */

static uint32_t rng_next(arp_t *a)
{
    if (!a->rng_seeded) {
        a->rng_seeded = 1;
        a->rng ^= 0x1234567u;
    }
    a->rng = a->rng * 0x0BB38435u + 0x3619636Bu;
    return a->rng;
}

static unsigned rng_index(arp_t *a, unsigned n)
{
    return (unsigned)(((rng_next(a) >> 27) + 1) % n);
}

static unsigned rng_bits2(arp_t *a)
{
    return (rng_next(a) >> 20) & 3u;
}

static int8_t random_octave(arp_t *a)
{
    int o = (int)rng_bits2(a);
    int r = a->range;
    if (r < 0)
        o = -o;
    if ((r >= 0 && o > r) || (r < 0 && o < r))
        o = 0;
    return (int8_t)o;
}

static void sorted_order(const note_list_t *l, uint8_t idx[ARP_MAX_NOTES])
{
    for (unsigned i = 0; i < l->n; i++)
        idx[i] = (uint8_t)i;
    for (unsigned i = 0; i < l->n; i++) {
        unsigned m = i;
        for (unsigned j = i + 1; j < l->n; j++)
            if (l->note[idx[j]] < l->note[idx[m]])
                m = j;
        uint8_t t = idx[i];
        idx[i] = idx[m];
        idx[m] = t;
    }
}

static void put(arp_t *a, unsigned *len, uint8_t note, uint8_t vel)
{
    if (*len >= SEQ_STEPS)
        return;
    seq_set_step(&a->pattern, *len, &note, &vel, 1, 0);
    (*len)++;
}

/* ---------------- pattern ---------------- */

void arp_build(arp_t *a)
{
    sequence_t *p = &a->pattern;
    const note_list_t *l = &a->held;
    unsigned n = l->n, len = 0;
    uint8_t s[ARP_MAX_NOTES];

    a->dirty = 0;
    seq_clear_steps(p);
    if (n == 0) {
        p->length = 1;
        a->octave = 0;
        if (a->player)
            a->player->octave_shift = 0;
        return;
    }
    sorted_order(l, s);

    switch (a->mode) {
    case ARP_DOWN:
        for (unsigned i = n; i-- > 0;)
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        break;
    case ARP_INCLUSIVE:
        for (unsigned i = 0; i < n; i++)
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        for (unsigned i = n; n > 1 && i-- > 0;)
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        break;
    case ARP_EXCLUSIVE:
        for (unsigned i = 0; i < n; i++)
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        for (unsigned i = n - 1; n > 2 && i-- > 1;)
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        break;
    case ARP_RANDOM:
        if (a->random_variant == 0) {
            unsigned i = rng_index(a, n);
            put(a, &len, l->note[i], l->vel[i]);
        } else if (a->random_variant == 1) {
            for (unsigned k = 0; k < a->random_len; k++) {
                unsigned i = rng_index(a, n);
                int note = l->note[i] + 12 * random_octave(a);
                put(a, &len, note > 127 || note < 0 ? l->note[i] : (uint8_t)note, l->vel[i]);
            }
        } else {
            for (unsigned i = 0; i < n; i++)
                put(a, &len, l->note[i], l->vel[i]);
        }
        break;
    case ARP_ORDER:
        for (unsigned i = 0; i < n; i++)
            put(a, &len, l->note[i], l->vel[i]);
        break;
    case ARP_UP2:
        for (unsigned i = 0; i < n; i++) {
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        }
        break;
    case ARP_DOWN2:
        for (unsigned i = n; i-- > 0;) {
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        }
        break;
    default: /* ARP_UP */
        for (unsigned i = 0; i < n; i++)
            put(a, &len, l->note[s[i]], l->vel[s[i]]);
        break;
    }
    if (n == 1 && !(a->mode == ARP_RANDOM && a->random_variant == 1))
        len = 1;
    p->length = (uint8_t)(len ? len : 1);
    if (a->walk_pos >= p->length)
        a->walk_pos = 0;
}

static void changed(arp_t *a)
{
    /* Stopped, or nothing left to play: apply now. Otherwise at the loop point. */
    if (!a->running || a->held.n == 0)
        arp_build(a);
    else
        a->dirty = 1;
}

static void octave_advance(arp_t *a, int reset)
{
    int8_t r = a->range;
    if (a->mode == ARP_RANDOM && a->random_variant == 1)
        a->octave = 0;
    else if (a->mode == ARP_RANDOM && a->random_variant == 0)
        a->octave = random_octave(a);
    else if (reset || r == 0)
        a->octave = 0;
    else if (r > 0)
        a->octave = a->octave >= r ? 0 : (int8_t)(a->octave + 1);
    else
        a->octave = a->octave <= r ? 0 : (int8_t)(a->octave - 1);
    if (a->player)
        a->player->octave_shift = (int8_t)(12 * a->octave);
}

uint8_t arp_step_hook(void *ctx, uint8_t step, int wrapped)
{
    arp_t *a = ctx;
    if (wrapped) {
        if (a->dirty || (a->mode == ARP_RANDOM && a->random_variant == 0))
            arp_build(a);
        octave_advance(a, 0);
    }
    if (a->mode == ARP_RANDOM && a->random_variant == 2 && a->pattern.length > 1) {
        unsigned len = a->pattern.length;
        switch (rng_bits2(a)) {
        case 0: a->walk_pos = (uint8_t)((a->walk_pos + len - 1) % len); break;
        case 1: break;
        default: a->walk_pos = (uint8_t)((a->walk_pos + 1) % len); break;
        }
        return a->walk_pos;
    }
    return step;
}

/* ---------------- control ---------------- */

void arp_init(arp_t *a, seq_player_t *player)
{
    *a = (arp_t){0};
    a->player = player;
    a->random_len = 16;
    seq_init_empty(&a->pattern, 0);
    a->pattern.flags = 0;
    if (player) {
        player->hook = arp_step_hook;
        player->hook_ctx = a;
    }
}

void arp_seed(arp_t *a, uint32_t seed)
{
    a->rng = seed;
    a->rng_seeded = 1;
}

void arp_note_on(arp_t *a, uint8_t note, uint8_t vel)
{
    a->keys_down++;
    if (a->hold && a->held.n && a->released.n == a->held.n) {
        /* every latched key was released: a new chord replaces the old one */
        note_list_clear(&a->held);
        note_list_clear(&a->released);
    }
    note_list_remove(&a->released, note);
    note_list_add(&a->held, note, vel);
    changed(a);
}

void arp_note_off(arp_t *a, uint8_t note)
{
    if (a->keys_down)
        a->keys_down--;
    if (a->hold) {
        if (note_list_has(&a->held, note))
            note_list_add(&a->released, note, 0);
        return;
    }
    if (note_list_remove(&a->held, note))
        changed(a);
}

void arp_set_hold(arp_t *a, int on)
{
    on = on && a->running; /* forced off while stopped */
    if (a->hold && !on) {
        for (unsigned i = 0; i < a->released.n; i++)
            note_list_remove(&a->held, a->released.note[i]);
        note_list_clear(&a->released);
        changed(a);
    }
    a->hold = (uint8_t)on;
}

void arp_set_mode(arp_t *a, uint8_t mode)
{
    a->mode = mode & 7;
    changed(a);
}

void arp_set_range(arp_t *a, int8_t range)
{
    a->range = range < -3 ? -3 : range > 3 ? 3 : range;
    octave_advance(a, 1);
    changed(a);
}

void arp_set_random_variant(arp_t *a, uint8_t variant)
{
    a->random_variant = variant > 2 ? 2 : variant;
    changed(a);
}

void arp_transport(arp_t *a, int running)
{
    a->running = (uint8_t)(running != 0);
    if (running) {
        a->walk_pos = 0;
        octave_advance(a, 1);
        arp_build(a);
        return;
    }
    a->hold = 0;
    note_list_clear(&a->held);
    note_list_clear(&a->released);
    a->keys_down = 0;
    octave_advance(a, 1);
    arp_build(a);
}
