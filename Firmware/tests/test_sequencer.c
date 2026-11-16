#include "test.h"
#include "../src/core/seq_player.h"

typedef struct {
    uint32_t tick;
    int on;
    uint8_t note, vel;
} ev_t;

static ev_t evs[512];
static int n_ev;
static uint32_t now;

static void on_note(void *ctx, int on, uint8_t note, uint8_t vel)
{
    (void)ctx;
    if (n_ev < 512)
        evs[n_ev++] = (ev_t){now, on, note, vel};
}

static void run(seq_player_t *p, uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; i++, now++)
        seq_player_tick(p);
}

static void reset_log(void)
{
    n_ev = 0;
    now = 0;
}

static void make_seq(sequence_t *s, const uint8_t *notes, unsigned len)
{
    seq_init_empty(s, 0);
    for (unsigned i = 0; i < len; i++) {
        uint8_t v = 100;
        if (notes[i] == SEQ_NOTE_REST)
            seq_set_rest(s, i, 0x40);
        else
            seq_set_step(s, i, &notes[i], &v, 1, 0);
    }
    seq_set_length(s, len, 0);
}

static void test_basic_loop(void)
{
    sequence_t s;
    seq_player_t p;
    const uint8_t notes[4] = {60, 62, SEQ_NOTE_REST, 64};
    make_seq(&s, notes, 4);
    seq_player_init(&p, on_note, 0);
    reset_log();
    seq_player_start(&p, &s);
    run(&p, 480 * 2); /* two loops of 4 x 1/16 */

    /* loop 1: 60 on@0 off@59, 62 on@120 off@179, rest, 64 on@360 off@419 */
    CHECK_EQ(n_ev, 12);
    CHECK(evs[0].on && evs[0].note == 60 && evs[0].tick == 0 && evs[0].vel == 100);
    CHECK(!evs[1].on && evs[1].note == 60 && evs[1].tick == 59);
    CHECK(evs[2].on && evs[2].note == 62 && evs[2].tick == 120);
    CHECK(evs[4].on && evs[4].note == 64 && evs[4].tick == 360);
    CHECK(evs[6].on && evs[6].note == 60 && evs[6].tick == 480);
}

static void test_gate_and_swing(void)
{
    sequence_t s;
    seq_player_t p;
    const uint8_t notes[2] = {60, 62};
    make_seq(&s, notes, 2);
    seq_set_gate(&s, 1); /* minimum gate is 15 ticks */
    seq_player_init(&p, on_note, 0);
    reset_log();
    seq_player_start(&p, &s);
    run(&p, 240);
    CHECK_EQ(evs[1].tick, 15);

    seq_set_gate(&s, 50);
    seq_set_swing(&s, 75); /* pairLen = 2*120*75/100 = 180 */
    reset_log();
    seq_player_start(&p, &s);
    run(&p, 240);
    CHECK_EQ(n_ev, 4);
    CHECK_EQ(evs[0].tick, 0);
    CHECK_EQ(evs[1].tick, 90);  /* early step gate = 180*50/100 */
    CHECK_EQ(evs[2].tick, 180); /* late step delayed by pairLen - T */
    CHECK_EQ(evs[3].tick, 210); /* (240-180)*50/100 */
}

static void test_tie(void)
{
    sequence_t s;
    seq_player_t p;
    const uint8_t notes[3] = {60, 60, 62};
    make_seq(&s, notes, 3);
    s.step[1][0].vel |= SEQ_VEL_TIE_FLAG; /* convention 0: step 1 continues step 0 */
    seq_player_init(&p, on_note, 0);
    reset_log();
    seq_player_start(&p, &s);
    run(&p, 360);
    /* 60 on@0, held through step 1 (no re-attack), off at step 1's gate end, then 62 */
    CHECK_EQ(n_ev, 4);
    CHECK(evs[0].on && evs[0].note == 60 && evs[0].tick == 0);
    CHECK(!evs[1].on && evs[1].note == 60 && evs[1].tick == 179);
    CHECK(evs[2].on && evs[2].note == 62 && evs[2].tick == 240);
    CHECK(!evs[3].on && evs[3].note == 62 && evs[3].tick == 299);

    /* legato into a different note: new note on before the old one goes off */
    s.step[2][0].vel |= SEQ_VEL_TIE_FLAG;
    reset_log();
    seq_player_start(&p, &s);
    run(&p, 360);
    CHECK_EQ(n_ev, 4);
    CHECK(evs[1].on && evs[1].note == 62 && evs[1].tick == 240);
    CHECK(!evs[2].on && evs[2].note == 60 && evs[2].tick == 240);
}

static void test_transpose_and_next_seq(void)
{
    sequence_t a, b;
    seq_player_t p;
    const uint8_t na[2] = {60, 61}, nb[1] = {70};
    make_seq(&a, na, 2);
    make_seq(&b, nb, 1);
    seq_player_init(&p, on_note, 0);
    p.transpose = 70; /* 130 folds to 118 */
    reset_log();
    seq_player_start(&p, &a);
    run(&p, 1);
    CHECK_EQ(evs[0].note, 118);
    p.transpose = 0;

    seq_player_select(&p, &b); /* mode End: switch at the wrap */
    run(&p, 239);
    reset_log();
    run(&p, 1);
    CHECK(evs[0].on && evs[0].note == 70);
}

static void test_division_change(void)
{
    sequence_t s;
    seq_player_t p;
    const uint8_t notes[4] = {60, 61, 62, 63};
    make_seq(&s, notes, 4);
    seq_player_init(&p, on_note, 0);
    seq_player_set_division(&p, 2);
    reset_log();
    seq_player_start(&p, &s);
    run(&p, 130);
    seq_player_set_division(&p, 1); /* 1/8: applied at tick 240 */
    run(&p, 110);
    CHECK_EQ(p.T, 120);
    run(&p, 1);
    CHECK_EQ(p.T, 240);
    CHECK_EQ(p.step, 2);
}

static void test_step_record(void)
{
    sequence_t s;
    step_rec_t r;
    seq_init_empty(&s, 0);
    step_rec_arm(&r, &s, 0);
    step_rec_note_on(&r, 60, 90);
    step_rec_note_on(&r, 64, 80);
    step_rec_note_off(&r, 60);
    CHECK_EQ(s.length, 0);
    step_rec_note_off(&r, 64);
    CHECK_EQ(s.length, 1);
    CHECK_EQ(s.step[0][0].note, 60);
    CHECK_EQ(s.step[0][1].note, 64);
    CHECK_EQ(s.step[0][2].note, SEQ_NOTE_EMPTY);
    step_rec_tap(&r, 1);
    step_rec_tap(&r, 0);
    CHECK_EQ(s.length, 2);
    CHECK(seq_step_is_rest(&s, 1));

    /* hold a key and tap twice: note + two tie steps */
    step_rec_note_on(&r, 67, 100);
    step_rec_tap(&r, 1);
    step_rec_tap(&r, 0);
    step_rec_tap(&r, 1);
    step_rec_tap(&r, 0);
    step_rec_note_off(&r, 67);
    CHECK_EQ(s.length, 5);
    CHECK_EQ(s.step[2][0].note, 67);
    CHECK(!(s.step[2][0].vel & SEQ_VEL_TIE_FLAG));
    CHECK(s.step[3][0].vel & SEQ_VEL_TIE_FLAG);
    CHECK(s.step[4][0].vel & SEQ_VEL_TIE_FLAG);

    step_rec_delete_last(&r);
    CHECK_EQ(s.length, 4);
    CHECK(s.flags & SEQ_FLAG_DIRTY);
}

static void test_rt_record(void)
{
    sequence_t s;
    rt_rec_t r;
    const uint8_t notes[4] = {60, 61, 62, 63};
    make_seq(&s, notes, 4);
    rt_rec_reset(&r);
    rt_rec_note_on(&r, 70, 100);
    rt_rec_write(&r, &s, 1);
    rt_rec_write(&r, &s, 2); /* still held: tie */
    rt_rec_note_off(&r, 70);
    rt_rec_write(&r, &s, 3); /* nothing held, no tap: keep */
    CHECK_EQ(s.step[1][0].note, 70);
    CHECK(!(s.step[1][0].vel & SEQ_VEL_TIE_FLAG));
    CHECK_EQ(s.step[2][0].note, 70);
    CHECK(s.step[2][0].vel & SEQ_VEL_TIE_FLAG);
    CHECK_EQ(s.step[3][0].note, 63);
    rt_rec_tap(&r);
    rt_rec_write(&r, &s, 0);
    CHECK(seq_step_is_rest(&s, 0));
}

static void test_length(void)
{
    sequence_t s;
    const uint8_t notes[4] = {60, 61, 62, 63};
    make_seq(&s, notes, 4);
    seq_set_length(&s, 2, 0);
    seq_set_length(&s, 4, 0); /* data beyond the old length survives */
    CHECK_EQ(s.step[3][0].note, 63);
    seq_set_length(&s, 2, 0);
    seq_set_length(&s, 4, 1); /* overwrite mode */
    CHECK(seq_step_is_rest(&s, 3));
    seq_set_length(&s, 100, 0);
    CHECK_EQ(s.length, 64);
}

int main(void)
{
    test_basic_loop();
    test_gate_and_swing();
    test_tie();
    test_transpose_and_next_seq();
    test_division_change();
    test_step_record();
    test_rt_record();
    test_length();
    return test_report("sequencer");
}
