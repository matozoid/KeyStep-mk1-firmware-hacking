#include "test.h"
#include "../src/core/arp.h"
#include "../src/core/chord.h"

static uint8_t pat(const arp_t *a, unsigned i)
{
    return a->pattern.step[i][0].note;
}

static void hold3(arp_t *a)
{
    arp_note_on(a, 64, 100);
    arp_note_on(a, 60, 90);
    arp_note_on(a, 67, 80);
}

static void test_modes(void)
{
    arp_t a;
    arp_init(&a, 0);
    hold3(&a);

    arp_set_mode(&a, ARP_UP);
    CHECK_EQ(a.pattern.length, 3);
    CHECK(pat(&a, 0) == 60 && pat(&a, 1) == 64 && pat(&a, 2) == 67);
    CHECK_EQ(a.pattern.step[0][0].vel, 90);

    arp_set_mode(&a, ARP_DOWN);
    CHECK(pat(&a, 0) == 67 && pat(&a, 2) == 60);

    arp_set_mode(&a, ARP_INCLUSIVE);
    CHECK_EQ(a.pattern.length, 6);
    CHECK(pat(&a, 2) == 67 && pat(&a, 3) == 67 && pat(&a, 5) == 60);

    arp_set_mode(&a, ARP_EXCLUSIVE);
    CHECK_EQ(a.pattern.length, 4);
    CHECK(pat(&a, 2) == 67 && pat(&a, 3) == 64);

    arp_set_mode(&a, ARP_ORDER);
    CHECK(pat(&a, 0) == 64 && pat(&a, 1) == 60 && pat(&a, 2) == 67);

    arp_set_mode(&a, ARP_UP2);
    CHECK_EQ(a.pattern.length, 6);
    CHECK(pat(&a, 0) == 60 && pat(&a, 1) == 60 && pat(&a, 2) == 64);

    arp_set_mode(&a, ARP_DOWN2);
    CHECK(pat(&a, 0) == 67 && pat(&a, 1) == 67 && pat(&a, 5) == 60);

    arp_seed(&a, 42);
    arp_set_random_variant(&a, 1);
    arp_set_mode(&a, ARP_RANDOM);
    CHECK_EQ(a.pattern.length, 16);
    for (unsigned i = 0; i < 16; i++)
        CHECK(pat(&a, i) % 12 == 0 || pat(&a, i) % 12 == 4 || pat(&a, i) % 12 == 7);

    arp_set_random_variant(&a, 0);
    CHECK_EQ(a.pattern.length, 1);

    arp_note_off(&a, 60);
    arp_note_off(&a, 64);
    arp_note_off(&a, 67);
    CHECK_EQ(a.held.n, 0);
    CHECK(seq_step_is_rest(&a.pattern, 0));

    /* exclusive with two notes: no repeats */
    arp_set_mode(&a, ARP_EXCLUSIVE);
    arp_note_on(&a, 60, 100);
    arp_note_on(&a, 62, 100);
    CHECK_EQ(a.pattern.length, 2);
}

static void test_hold(void)
{
    arp_t a;
    arp_init(&a, 0);
    arp_set_hold(&a, 1);
    CHECK_EQ(a.hold, 0); /* forced off while stopped */

    arp_transport(&a, 1);
    arp_set_hold(&a, 1);
    hold3(&a);
    arp_note_off(&a, 60);
    arp_note_off(&a, 64);
    CHECK_EQ(a.held.n, 3);
    arp_note_on(&a, 72, 100); /* 67 still down: legato add */
    CHECK_EQ(a.held.n, 4);
    arp_note_off(&a, 67);
    arp_note_off(&a, 72);
    arp_note_on(&a, 48, 100); /* all released: new chord */
    CHECK_EQ(a.held.n, 1);
    CHECK_EQ(a.held.note[0], 48);

    arp_note_on(&a, 50, 100);
    arp_note_off(&a, 48);
    arp_set_hold(&a, 0); /* released notes go, held key stays */
    CHECK_EQ(a.held.n, 1);
    CHECK_EQ(a.held.note[0], 50);

    arp_transport(&a, 0);
    CHECK_EQ(a.held.n, 0);
}

static int n_on;
static uint8_t on_notes[64];

static void on_note(void *ctx, int on, uint8_t note, uint8_t vel)
{
    (void)ctx;
    (void)vel;
    if (on && n_on < 64)
        on_notes[n_on++] = note;
}

static void test_octaves_with_player(void)
{
    seq_player_t p;
    arp_t a;
    seq_player_init(&p, on_note, 0);
    arp_init(&a, &p);
    arp_note_on(&a, 60, 100);
    arp_note_on(&a, 62, 100);
    arp_set_range(&a, 2);
    arp_transport(&a, 1);
    seq_player_start(&p, &a.pattern);
    n_on = 0;
    for (int t = 0; t < 120 * 6; t++)
        seq_player_tick(&p);
    CHECK_EQ(n_on, 6);
    CHECK(on_notes[0] == 60 && on_notes[1] == 62);
    CHECK(on_notes[2] == 72 && on_notes[3] == 74);
    CHECK(on_notes[4] == 84 && on_notes[5] == 86);

    /* note added while running is applied at the loop point */
    arp_note_on(&a, 64, 100);
    CHECK_EQ(a.pattern.length, 2);
    n_on = 0;
    for (int t = 0; t < 120 * 2; t++)
        seq_player_tick(&p);
    CHECK_EQ(a.pattern.length, 3);
    CHECK_EQ(on_notes[0], 60); /* octave cycle wrapped back to 0 */

    /* chord expansion on arp steps */
    static const uint8_t ch[3] = {60, 64, 67};
    p.chord = ch;
    p.chord_n = 3;
    arp_set_range(&a, 0);
    seq_player_restart(&p);
    n_on = 0;
    seq_player_tick(&p);
    CHECK_EQ(n_on, 3);
    CHECK(on_notes[0] == 60 && on_notes[1] == 64 && on_notes[2] == 67);
}

static void test_chord(void)
{
    chord_t c;
    uint8_t out[CHORD_MAX];
    chord_init(&c);
    CHECK_EQ(chord_expand(&c, 50, out), 1);

    chord_capture_begin(&c);
    chord_capture_note(&c, 64);
    chord_capture_note(&c, 60); /* lower: becomes root */
    chord_capture_note(&c, 67);
    chord_capture_note(&c, 64);
    chord_capture_end(&c);
    CHECK_EQ(c.state, CHORD_ON);
    CHECK_EQ(c.n, 3);
    CHECK_EQ(c.notes[0], 60);

    unsigned n = chord_expand(&c, 62, out);
    CHECK_EQ(n, 3);
    CHECK(out[0] == 62 && out[1] == 66 && out[2] == 69);
    n = chord_expand(&c, 125, out); /* folds down */
    CHECK(out[1] == 117 && out[2] == 120);

    chord_capture_begin(&c); /* on, nothing played: off */
    chord_capture_end(&c);
    CHECK_EQ(c.state, CHORD_OFF);
    chord_capture_begin(&c); /* off, nothing played: on with old chord */
    chord_capture_end(&c);
    CHECK_EQ(c.state, CHORD_ON);
    CHECK_EQ(c.n, 3);
}

int main(void)
{
    test_modes();
    test_hold();
    test_octaves_with_player();
    test_chord();
    return test_report("arp");
}
