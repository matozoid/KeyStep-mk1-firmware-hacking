#include "chord.h"

void chord_init(chord_t *c)
{
    *c = (chord_t){0};
    c->notes[0] = 60;
    c->n = 1;
}

void chord_capture_begin(chord_t *c)
{
    c->state_before = c->state;
    c->state = CHORD_CAPTURING;
    c->captured = 0;
}

void chord_capture_end(chord_t *c)
{
    if (c->captured || c->state_before == CHORD_OFF)
        c->state = CHORD_ON;
    else
        c->state = CHORD_OFF;
}

void chord_capture_note(chord_t *c, uint8_t note)
{
    if (c->state != CHORD_CAPTURING)
        return;
    if (!c->captured) {
        c->captured = 1;
        c->n = 0;
    }
    for (unsigned i = 0; i < c->n; i++)
        if (c->notes[i] == note)
            return;
    if (c->n >= CHORD_MAX)
        return;
    if (c->n && note < c->notes[0]) {
        /* the root is the lowest captured note */
        c->notes[c->n++] = c->notes[0];
        c->notes[0] = note;
    } else {
        c->notes[c->n++] = note;
    }
}

unsigned chord_expand(const chord_t *c, uint8_t played, uint8_t out[CHORD_MAX])
{
    unsigned n = 0;
    for (unsigned i = 0; i < c->n; i++) {
        int v = played + (c->notes[i] - c->notes[0]);
        while (v > 127)
            v -= 12;
        while (v < 0)
            v += 12;
        uint8_t o = (uint8_t)v;
        unsigned j;
        for (j = 0; j < n && out[j] != o; j++)
            ;
        if (j == n)
            out[n++] = o;
    }
    return n;
}
