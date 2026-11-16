#include <string.h>
#include "test.h"
#include "../src/core/app.h"

/* ---- stub outputs ---- */

typedef struct {
    uint8_t ports, len, m[3];
} msg_t;

static msg_t out[4096];
static int n_out;
static uint8_t sx_out[256];
static int sx_len, n_sx;
static uint16_t last_pitch, last_mod;
static int gate, sync_clock, sync_run, boot_req;
static uint8_t led_bits;

static void o_midi(void *c, uint8_t ports, const uint8_t *m, uint8_t len)
{
    (void)c;
    if (n_out < 4096) {
        out[n_out].ports = ports;
        out[n_out].len = len;
        memcpy(out[n_out].m, m, len > 3 ? 3 : len);
        n_out++;
    }
}
static void o_sysex(void *c, const uint8_t *d, uint16_t len)
{
    (void)c;
    n_sx++;
    if (len <= sizeof sx_out) {
        memcpy(sx_out, d, len);
        sx_len = len;
    }
}
static void o_pitch(void *c, uint16_t v) { (void)c; last_pitch = v; }
static void o_mod(void *c, uint16_t v) { (void)c; last_mod = v; }
static void o_gate(void *c, int on) { (void)c; gate = on; }
static uint8_t gate_type = 0xFF;
static void o_gate_type(void *c, uint8_t t) { (void)c; gate_type = t; }
static void o_sync(void *c, int clk, int run) { (void)c; sync_clock = clk; sync_run = run; }
static void o_leds(void *c, uint8_t b, uint8_t br) { (void)c; (void)br; led_bits = b; }
static void o_boot(void *c) { (void)c; boot_req = 1; }
static uint8_t hw_test_req;
static int o_hw_test(void *c, uint8_t t) { (void)c; hw_test_req = t; return t != 0x72; }

static const app_io_t io = {0, o_midi, o_sysex, o_pitch, o_mod, o_gate, o_gate_type, o_sync, o_leds, o_boot,
                            o_hw_test};

static app_t a;
static uint32_t t_us;

static void clear(void)
{
    n_out = 0;
    n_sx = 0;
    sx_len = 0;
}

static void boot(void)
{
    static uint8_t erased[0x800];
    memset(erased, 0xFF, sizeof erased);
    const uint8_t *imgs[SEQ_COUNT];
    for (unsigned i = 0; i < SEQ_COUNT; i++)
        imgs[i] = erased;
    t_us = 1000;
    app_init(&a, &io, erased, erased, imgs);
    app_switches(&a, APP_SYNC_INTERNAL, 0);
    app_poll(&a, t_us);
    clear();
}

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < ms * 4; i++) {
        t_us += 250;
        app_poll(&a, t_us);
    }
}

static void tap(unsigned b)
{
    app_buttons(&a, (uint8_t)(a.buttons | (1u << b)));
    app_buttons(&a, (uint8_t)(a.buttons & ~(1u << b)));
}

static int count(uint8_t status, int note)
{
    int n = 0;
    for (int i = 0; i < n_out; i++)
        if (out[i].m[0] == status && (note < 0 || out[i].m[1] == note))
            n++;
    return n;
}

static int find(uint8_t status, int d1)
{
    for (int i = 0; i < n_out; i++)
        if (out[i].m[0] == status && (d1 < 0 || out[i].m[1] == d1))
            return i;
    return -1;
}

/* ---- tests ---- */

static void test_keys(void)
{
    boot();
    app_key(&a, 0, 100); /* top key, octave 0 */
    int i = find(0x90, app_key_base_note(0));
    CHECK(i >= 0);
    CHECK_EQ(out[i].ports, APP_PORT_ALL);
    CHECK_EQ(out[i].m[2], 100);
    CHECK_EQ(gate, 1);
    app_key(&a, 0, 0);
    CHECK(find(0x80, app_key_base_note(0)) >= 0);
    CHECK_EQ(gate, 0);

    /* octave up; release after an octave change still ends the note that was played */
    clear();
    tap(APP_BTN_OCT_UP);
    CHECK_EQ(a.octave, 1);
    app_key(&a, 5, 90);
    tap(APP_BTN_OCT_DOWN);
    app_key(&a, 5, 0);
    CHECK_EQ(count(0x90, app_key_base_note(5) + 12), 1);
    CHECK_EQ(count(0x80, app_key_base_note(5) + 12), 1);

    /* both octave buttons: reset */
    a.octave = 3;
    app_buttons(&a, (1u << APP_BTN_OCT_UP) | (1u << APP_BTN_OCT_DOWN));
    app_buttons(&a, 0);
    CHECK_EQ(a.octave, 0);
}

static void test_shift_channel(void)
{
    boot();
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    app_key(&a, 31 - 4, 100); /* channel 5 */
    app_key(&a, 31 - 4, 0);
    app_buttons(&a, 0);
    CHECK_EQ(settings_get(&a.set, P_USER_CH), 4);
    CHECK_EQ(count(0x94, -1) + count(0x90, -1), 0); /* Shift + key plays nothing */
    app_key(&a, 0, 100);
    CHECK_EQ(count(0x94, -1), 1);
    run_ms(5);
    CHECK(app_save_request(&a) & APP_SAVE_SETTINGS);
}

static void test_chord_mode(void)
{
    boot();
    /* Shift + Hold held: keys are captured (and sound), not taken as Shift + key functions */
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    app_buttons(&a, (1u << APP_BTN_SHIFT) | (1u << APP_BTN_HOLD));
    uint8_t ch_before = settings_get(&a.set, P_USER_CH);
    app_key(&a, 31, 100); /* 41 */
    app_key(&a, 27, 100); /* 45 */
    app_key(&a, 24, 100); /* 48 */
    app_key(&a, 31, 0);
    app_key(&a, 27, 0);
    app_key(&a, 24, 0);
    CHECK_EQ(settings_get(&a.set, P_USER_CH), ch_before);
    CHECK_EQ(count(0x90, 41) + count(0x90, 45) + count(0x90, 48), 3);
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    app_buttons(&a, 0);
    CHECK_EQ(a.chord.state, CHORD_ON);
    CHECK_EQ(a.chord.n, 3);

    /* one key now plays the chord rooted on it */
    clear();
    app_key(&a, 19, 90); /* 53 */
    CHECK_EQ(count(0x90, 53), 1);
    CHECK_EQ(count(0x90, 57), 1);
    CHECK_EQ(count(0x90, 60), 1);
    app_key(&a, 19, 0);
    CHECK_EQ(count(0x80, 53) + count(0x80, 57) + count(0x80, 60), 3);

    /* Shift + Hold again without keys: chord mode off */
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    app_buttons(&a, (1u << APP_BTN_SHIFT) | (1u << APP_BTN_HOLD));
    app_buttons(&a, 0);
    CHECK_EQ(a.chord.state, CHORD_OFF);
    clear();
    app_key(&a, 19, 90);
    CHECK_EQ(count(0x90, -1), 1);
    app_key(&a, 19, 0);
}

static void test_cv_priority(void)
{
    boot();
    settings_set(&a.set, P_NOTE_PRIORITY, 0); /* low */
    app_key(&a, 10, 100);
    uint16_t low = last_pitch;
    app_key(&a, 2, 100); /* higher note */
    CHECK_EQ(last_pitch, low);
    app_key(&a, 10, 0);
    CHECK(last_pitch > low);
    CHECK_EQ(gate, 1);
    app_key(&a, 2, 0);
    CHECK_EQ(gate, 0);
}

static void test_transport_internal(void)
{
    boot();
    /* sequence 1: four steps C D E F */
    sequence_t *s = &a.seq[0];
    seq_init_empty(s, 0);
    static const uint8_t notes[4] = {60, 62, 64, 65};
    uint8_t v = 100;
    for (unsigned i = 0; i < 4; i++)
        seq_set_step(s, i, &notes[i], &v, 1, 0);
    seq_set_length(s, 4, 0);

    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PLAYING);
    CHECK(find(0xFA, -1) >= 0);
    CHECK_EQ(sync_run, 1);
    run_ms(1100); /* 120 bpm, 1/16: 8 steps in 1 s */
    CHECK(count(0xF8, -1) >= 48);
    CHECK(count(0x90, 60) >= 2);
    CHECK(count(0x90, 65) >= 1);
    CHECK_EQ(count(0x90, 61), 0);

    /* Play again pauses; Play once more continues */
    clear();
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PAUSED);
    CHECK(find(0xFC, -1) >= 0);
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PLAYING);
    CHECK(find(0xFB, -1) >= 0);

    /* Stop: transport CC 51 = 127 then 0 */
    clear();
    tap(APP_BTN_STOP);
    CHECK_EQ(a.transport, TR_STOPPED);
    int i = find(0xB0, 51);
    CHECK(i >= 0);
    CHECK_EQ(out[i].m[2], 127);
    CHECK(find(0xFC, -1) >= 0);
    CHECK_EQ(sync_run, 0);

    /* Stop three times quickly: all notes off on 16 channels */
    clear();
    tap(APP_BTN_STOP);
    run_ms(100);
    tap(APP_BTN_STOP);
    for (uint8_t ch = 0; ch < 16; ch++)
        CHECK(find((uint8_t)(0xB0 | ch), 123) >= 0);
}

static void test_transpose(void)
{
    boot();
    sequence_t *s = &a.seq[0];
    seq_init_empty(s, 0);
    uint8_t n = 60, v = 100;
    seq_set_step(s, 0, &n, &v, 1, 0);
    seq_set_length(s, 1, 0);
    tap(APP_BTN_PLAY);
    app_key(&a, (uint8_t)(app_key_base_note(0) - 62), 100); /* D -> +2 */
    clear();
    run_ms(300);
    CHECK(count(0x90, 62) >= 1);
    CHECK_EQ(count(0x90, 60), 0);
}

static void rec_keys(const uint8_t *keys, unsigned n)
{
    app_buttons(&a, (uint8_t)(a.buttons | (1u << APP_BTN_REC)));
    for (unsigned i = 0; i < n; i++) {
        app_key(&a, keys[i], 100);
        app_key(&a, keys[i], 0);
    }
    app_buttons(&a, (uint8_t)(a.buttons & ~(1u << APP_BTN_REC)));
}

/* Rec held + channel keys sets the sequence length (key 31 = 1 step ... key 16 = 16 steps). */
static void test_rec_length(void)
{
    boot();
    tap(APP_BTN_REC);
    for (uint8_t k = 0; k < 4; k++) {
        app_key(&a, k, 100);
        app_key(&a, k, 0);
    }
    tap(APP_BTN_REC);
    CHECK_EQ(a.seq[0].length, 4);
    uint8_t n0 = a.seq[0].step[0][0].note, n3 = a.seq[0].step[3][0].note;

    n_out = 0;
    rec_keys((const uint8_t[]){30}, 1); /* 2 steps */
    CHECK_EQ(a.seq[0].length, 2);
    CHECK_EQ(a.recording, 0);          /* Rec did not toggle record */
    CHECK_EQ(count(0x90, -1), 0);      /* keys were silent */
    CHECK_EQ(a.seq[0].step[0][0].note, n0);

    rec_keys((const uint8_t[]){28, 28}, 2); /* 4 + 4: content beyond the old end is kept */
    CHECK_EQ(a.seq[0].length, 8);
    CHECK_EQ(a.seq[0].step[3][0].note, n3);
    CHECK(seq_step_is_rest(&a.seq[0], 4));

    rec_keys((const uint8_t[]){16, 16, 24, 30}, 4); /* 16 + 16 + 8 + 2 */
    CHECK_EQ(a.seq[0].length, 42);
    rec_keys((const uint8_t[]){16, 16, 16, 16, 16}, 5); /* capped */
    CHECK_EQ(a.seq[0].length, 64);

    rec_keys((const uint8_t[]){5}, 1); /* not a channel key: nothing entered, Rec toggles */
    CHECK_EQ(a.recording, 1);
    CHECK_EQ(a.seq[0].length, 64);
    /* freshly armed: a length gives an empty sequence of that size, record stays on */
    rec_keys((const uint8_t[]){24}, 1);
    CHECK_EQ(a.recording, 1);
    CHECK_EQ(a.seq[0].length, 8);
    CHECK(seq_step_is_rest(&a.seq[0], 0));
    tap(APP_BTN_REC);
    CHECK_EQ(a.recording, 0);
}

static void test_step_record(void)
{
    boot();
    tap(APP_BTN_REC);
    CHECK_EQ(a.recording, 1);
    app_key(&a, 0, 100);
    app_key(&a, 0, 0);
    app_key(&a, 1, 100);
    app_key(&a, 1, 0);
    tap(APP_BTN_TAP); /* rest */
    tap(APP_BTN_REC);
    CHECK_EQ(a.recording, 0);
    CHECK_EQ(a.seq[0].length, 3);
    CHECK_EQ(a.seq[0].step[0][0].note, app_key_base_note(0));
    CHECK_EQ(a.seq[0].step[1][0].note, app_key_base_note(1));
    CHECK(seq_step_is_rest(&a.seq[0], 2));
    CHECK(a.seq[0].flags & SEQ_FLAG_DIRTY);
    run_ms(10001);
    uint16_t m = app_save_request(&a);
    CHECK(m & APP_SAVE_SEQ(0));
    app_save_done(&a, m);
    CHECK(!(a.seq[0].flags & SEQ_FLAG_DIRTY));
    CHECK_EQ(app_save_request(&a), 0);
}

static void test_aftertouch(void)
{
    boot();
    CHECK_EQ(app_at_curve(1, 0), 0);
    CHECK_EQ(app_at_curve(1, 4000), 127);
    for (uint8_t c = 0; c < 3; c++)
        for (uint16_t r = 256; r < 2400; r += 16)
            CHECK(app_at_curve(c, r) <= app_at_curve(c, (uint16_t)(r + 16)));

    app_key(&a, 0, 100);
    clear();
    app_aftertouch(&a, 4000);
    run_ms(200);
    CHECK(count(0xD0, -1) > 10); /* slewed rise, several messages */
    CHECK_EQ(a.at_value, 127);
}

static void test_midi_in(void)
{
    boot();
    static const uint8_t on[3] = {0x90, 64, 100}, off[3] = {0x80, 64, 0};
    app_midi_in(&a, APP_PORT_DIN, on, 3);
    CHECK_EQ(gate, 1);
    app_midi_in(&a, APP_PORT_DIN, off, 3);
    CHECK_EQ(gate, 0);
    /* thru off by default? either way only DIN is the thru target */
    for (int i = 0; i < n_out; i++)
        CHECK_EQ(out[i].ports, APP_PORT_DIN);
}

static void test_sync_external(void)
{
    boot();
    tap(APP_BTN_REC);
    for (uint8_t k = 0; k < 8; k++) {
        app_key(&a, k, 100);
        app_key(&a, k, 0);
    }
    tap(APP_BTN_REC);
    app_switches(&a, APP_SYNC_MIDI, 0);
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_ARMED);
    app_realtime_in(&a, APP_PORT_DIN, 0xFA);
    CHECK_EQ(a.transport, TR_PLAYING);
    for (int i = 0; i < 32; i++) {
        t_us += 20833;
        app_poll(&a, t_us);
        app_realtime_in(&a, APP_PORT_DIN, 0xF8);
    }
    CHECK(count(0xF8, -1) >= 24); /* forwarded */

    /* a master stop pauses: Continue resumes at the same step, Start begins again */
    unsigned step = seq_player_step(&a.player);
    CHECK(step > 0);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);
    CHECK_EQ(a.transport, TR_PAUSED);
    CHECK_EQ(seq_player_step(&a.player), step);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC); /* a second stop changes nothing */
    CHECK_EQ(a.transport, TR_PAUSED);
    app_realtime_in(&a, APP_PORT_DIN, 0xFB);
    CHECK_EQ(a.transport, TR_PLAYING);
    CHECK_EQ(seq_player_step(&a.player), step);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);
    app_realtime_in(&a, APP_PORT_DIN, 0xFA);
    CHECK_EQ(a.transport, TR_PLAYING);
    CHECK_EQ(seq_player_step(&a.player), 0);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);
    tap(APP_BTN_STOP);
    CHECK_EQ(a.transport, TR_STOPPED);

    /* armed and no clock yet: a master stop does not disarm */
    tap(APP_BTN_PLAY);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);
    CHECK_EQ(a.transport, TR_ARMED);
}

static void ext_clock(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        t_us += 20833;
        app_poll(&a, t_us);
        app_realtime_in(&a, APP_PORT_DIN, 0xF8);
    }
}

/* Arm to Start: under an external clock the sequence follows silently until Play. */
static void test_arm_to_start(void)
{
    boot();
    tap(APP_BTN_REC);
    for (uint8_t k = 0; k < 4; k++) {
        app_key(&a, k, 100);
        app_key(&a, k, 0);
    }
    tap(APP_BTN_REC);
    app_switches(&a, APP_SYNC_MIDI, 0);
    settings_set(&a.set, P_WAIT_BAR, 1);
    unsigned T = a.player.T / CLOCK_TICKS_PER_F8; /* clock pulses per step */

    /* master start: unit looks stopped, plays nothing, passes the clock on, counts along */
    clear();
    app_realtime_in(&a, APP_PORT_DIN, 0xFA);
    CHECK_EQ(a.transport, TR_FOLLOW);
    ext_clock(T * 2 + 1);
    CHECK_EQ(count(0x90, -1), 0);
    CHECK(count(0xF8, -1) >= (int)(T * 2));
    CHECK_EQ(seq_player_step(&a.player), 2);
    CHECK(led_bits & (1u << APP_LED_STOP));
    CHECK(!(led_bits & (1u << APP_LED_PLAY)));

    /* Play joins at the position it would have had: the next note is step 3 */
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PLAYING);
    clear();
    ext_clock(T);
    CHECK_EQ(count(0x90, -1), 1);
    CHECK_EQ(out[find(0x90, -1)].m[1], a.seq[0].step[3][0].note);

    /* Play again mutes, position keeps running */
    clear();
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_FOLLOW);
    CHECK_EQ(count(0x80, -1), 1);
    clear();
    ext_clock(T * 2);
    CHECK_EQ(count(0x90, -1), 0);
    CHECK(!(led_bits & (1u << APP_LED_STOP)));
    CHECK_EQ(seq_player_step(&a.player), 1);
    tap(APP_BTN_PLAY);
    clear();
    ext_clock(T);
    CHECK_EQ(out[find(0x90, -1)].m[1], a.seq[0].step[2][0].note);

    /* Stop while the master runs: silent, looks stopped, still following */
    tap(APP_BTN_STOP);
    CHECK_EQ(a.transport, TR_FOLLOW);
    clear();
    ext_clock(T);
    CHECK_EQ(count(0x90, -1), 0);
    CHECK(led_bits & (1u << APP_LED_STOP));
    CHECK_EQ(seq_player_step(&a.player), 3);

    /* master stop while following: really stopped */
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);
    CHECK_EQ(a.transport, TR_STOPPED);
    CHECK_EQ(seq_player_step(&a.player), 0);

    /* no master: Play arms, a running clock without a start message is enough */
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_ARMED);
    clear();
    ext_clock(1);
    CHECK_EQ(a.transport, TR_PLAYING);
    CHECK_EQ(count(0x90, -1), 1);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);

    /* setting off: a master start plays at once; internal sync ignores the setting */
    settings_set(&a.set, P_WAIT_BAR, 0);
    app_realtime_in(&a, APP_PORT_DIN, 0xFA);
    CHECK_EQ(a.transport, TR_PLAYING);
    app_realtime_in(&a, APP_PORT_DIN, 0xFC);
    settings_set(&a.set, P_WAIT_BAR, 1);
    app_switches(&a, APP_SYNC_INTERNAL, 0);
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PLAYING);
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PAUSED);
}

static void sysex(const uint8_t *f, uint8_t n)
{
    app_sysex_in(&a, APP_PORT_USB, f, n, 1);
}

static void test_sysex(void)
{
    boot();
    static const uint8_t idreq[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    sysex(idreq, sizeof idreq);
    CHECK_EQ(n_sx, 1);
    CHECK_EQ(sx_out[4], 0x02);
    CHECK_EQ(sx_out[7], 0x6B);

    /* set then get the user channel (code 0x01? use LED brightness 0x6B) */
    clear();
    static const uint8_t set[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x41, 0x6B, 40, 0xF7};
    sysex(set, sizeof set);
    CHECK_EQ(settings_get(&a.set, P_LED_BRIGHTNESS), 40);
    static const uint8_t get[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x01, 0x00, 0x41, 0x6B, 0xF7};
    sysex(get, sizeof get);
    CHECK_EQ(n_sx, 2); /* value + ACK */

    static const uint8_t upd[] = {0xF0, 0x5A, 0x57, 0x6E, 0x28, 0x3C, 0x4E, 0x3F, 0xF7};
    sysex(upd, sizeof upd);
    CHECK_EQ(boot_req, 1);

    /* note preview drives the CV voice; the 0x23 form carries a velocity, 0 releases */
    static const uint8_t pv_on[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x7D, 0x7A, 0x06, 60, 0xF7};
    sysex(pv_on, sizeof pv_on);
    run_ms(2);
    CHECK_EQ(gate, 1);
    uint16_t p60 = last_pitch;
    static const uint8_t pv_off[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x23, 0x7D, 0x7A, 0x06, 0x00, 60, 0, 0xF7};
    sysex(pv_off, sizeof pv_off);
    run_ms(2);
    CHECK_EQ(gate, 0);
    static const uint8_t pv_vel[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x23, 0x7D, 0x7A, 0x06, 0x00, 72, 100, 0xF7};
    sysex(pv_vel, sizeof pv_vel);
    run_ms(2);
    CHECK_EQ(gate, 1);
    CHECK(last_pitch > p60);
    sysex(pv_off, sizeof pv_off); /* releasing another note leaves 72 sounding */
    run_ms(2);
    CHECK_EQ(gate, 1);

    /* DIN SysEx is ignored */
    clear();
    app_sysex_in(&a, APP_PORT_DIN, idreq, sizeof idreq, 1);
    CHECK_EQ(n_sx, 0);
}

static void test_sysex_sequence(void)
{
    boot();
    /* MCC-style write of sequence 2, step 3: ties, velocities, notes, then the length */
    static const uint8_t tie[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x00, 0x63, 0x02, 0x01, 0x02, 0x01, 32,
                                  0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xF7};
    static const uint8_t vel[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x00, 0x65, 0x02, 0x01, 0x02, 0x03, 8,
                                  90, 80, 0, 0, 0, 0, 0, 0, 0xF7};
    static const uint8_t note[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x00, 0x66, 0x02, 0x01, 0x02, 0x03, 8,
                                   60, 64, 0, 0, 0, 0, 0, 0, 0xF7};
    static const uint8_t len[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x00, 0x60, 0x01, 0x01, 0x02, 1, 4, 0xF7};
    sysex(tie, sizeof tie);
    sysex(vel, sizeof vel);
    sysex(note, sizeof note);
    sysex(len, sizeof len);
    sequence_t *s = &a.seq[1];
    CHECK_EQ(s->length, 4);
    CHECK_EQ(s->step[2][0].note, 60);
    CHECK_EQ(s->step[2][0].vel, 0x80 | 90);
    CHECK_EQ(s->step[2][1].note, 64);
    CHECK_EQ(s->step[2][1].vel, 80);
    CHECK_EQ(s->step[2][2].note, SEQ_NOTE_EMPTY);
    CHECK(s->flags & SEQ_FLAG_DIRTY);
    CHECK_EQ(n_sx, 4); /* one ACK each */

    /* a block read answers with the data, then an ACK (layout checked in test_sysex_block_read) */
    clear();
    static const uint8_t rd[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0B, 0x00, 0x65, 0x02, 0x01, 0x02, 0x03, 8, 0xF7};
    sysex(rd, sizeof rd);
    CHECK_EQ(n_sx, 2);

    /* single set: velocity 0 clears slot 2 */
    static const uint8_t clr[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x25, 0x00, 0x65, 0x01, 0x02, 0x03, 0x02, 0, 0xF7};
    sysex(clr, sizeof clr);
    CHECK_EQ(s->step[2][1].note, SEQ_NOTE_EMPTY);
    /* single set of the length extends with rests */
    static const uint8_t l6[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x21, 0x00, 0x60, 0x01, 0x02, 6, 0xF7};
    sysex(l6, sizeof l6);
    CHECK_EQ(s->length, 6);
    CHECK_EQ(s->step[5][0].note, SEQ_NOTE_REST);
}

static uint8_t replies[8][128];
static int reply_len[8], n_replies;

static void o_sysex_log(void *c, const uint8_t *d, uint16_t len)
{
    (void)c;
    if (n_replies < 8 && len <= 128) {
        memcpy(replies[n_replies], d, len);
        reply_len[n_replies++] = len;
    }
}

static void test_sysex_block_read(void)
{
    boot();
    a.io.sysex = o_sysex_log;
    sequence_t *s = &a.seq[0];
    uint8_t notes[2] = {48, 55}, vels[2] = {70, 0x7F};
    seq_set_step(s, 0, notes, vels, 2, 0);
    static const uint8_t rd[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0B, 0x00, 0x66, 0x02, 0x01, 0x01, 0x01, 8, 0xF7};
    n_replies = 0;
    sysex(rd, sizeof rd);
    CHECK_EQ(n_replies, 2);
    CHECK_EQ(reply_len[0], 14 + 8 + 1);
    CHECK_EQ(replies[0][6], 0x0C);
    CHECK_EQ(replies[0][13], 8);
    CHECK_EQ(replies[0][14], 48);
    CHECK_EQ(replies[0][15], 55);
    CHECK_EQ(replies[0][22], 0xF7);
    CHECK_EQ(replies[1][6], 0x1C);
}

static void test_calibration(void)
{
    boot();
    static const uint8_t lo[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x08, 0x40, '3', 0x41, 0xF7};
    static const uint8_t hi[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x08, 0x40, '2', 0x41, 0xF7};
    static const uint8_t lo_m[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x08, 0x40, '3', 0x40, 0xF7};
    static const uint8_t hi_m[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x08, 0x40, '2', 0x40, 0xF7};
    app_touch(&a, 0, 0x0C);
    app_touch(&a, 1, 0x0B);
    sysex(lo, sizeof lo);
    sysex(lo_m, sizeof lo_m);
    app_touch(&a, 0, 0xB0);
    app_touch(&a, 1, 0xB4);
    sysex(hi, sizeof hi);
    CHECK_EQ(a.cal[0].lo, 0x0C);
    CHECK_EQ(a.cal[0].hi, 0xB0);
    CHECK_EQ(app_save_request(&a) & APP_SAVE_SETTINGS, 0); /* mod strip not done yet */
    sysex(hi_m, sizeof hi_m);
    CHECK_EQ(a.cal[1].lo, 0x0B);
    CHECK_EQ(a.cal[1].hi, 0xB4);
    run_ms(2);
    CHECK(app_save_request(&a) & APP_SAVE_SETTINGS);
}

static void test_leds(void)
{
    boot();
    run_ms(10);
    CHECK(led_bits & (1u << APP_LED_STOP));
    CHECK(!(led_bits & (1u << APP_LED_OCT_UP))); /* octave 0: both dark */
    CHECK(!(led_bits & (1u << APP_LED_OCT_DOWN)));
    /* Shift in Seq mode shows the keyboard mode: Transpose on Oct-, Kbd Play on Oct+ */
    app_switches(&a, APP_SYNC_INTERNAL, 0);
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    run_ms(10);
    CHECK(led_bits & (1u << APP_LED_OCT_DOWN));
    CHECK(!(led_bits & (1u << APP_LED_OCT_UP)));
    app_buttons(&a, (1u << APP_BTN_SHIFT) | (1u << APP_BTN_OCT_UP));
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    run_ms(10);
    CHECK(led_bits & (1u << APP_LED_OCT_UP));
    CHECK(!(led_bits & (1u << APP_LED_OCT_DOWN)));
    app_buttons(&a, 0);
    run_ms(10);
    CHECK(!(led_bits & (1u << APP_LED_OCT_UP)));
    tap(APP_BTN_PLAY);
    run_ms(10);
    CHECK(led_bits & (1u << APP_LED_PLAY));
    CHECK(!(led_bits & (1u << APP_LED_STOP)));
}

static void test_arp(void)
{
    boot();
    app_switches(&a, APP_SYNC_INTERNAL, 1);
    tap(APP_BTN_PLAY);
    app_key(&a, 12, 100);
    app_key(&a, 8, 100);
    clear();
    run_ms(1000);
    CHECK(count(0x90, app_key_base_note(12)) >= 2);
    CHECK(count(0x90, app_key_base_note(8)) >= 2);
    app_key(&a, 12, 0);
    app_key(&a, 8, 0);
    clear();
    run_ms(500);
    CHECK_EQ(count(0x90, -1), 0);
}

static void test_knobs(void)
{
    boot();
    /* stock detent windows: position 7 at the low end, 0 at the top, gaps keep the last one */
    app_knobs(&a, 2048, 0xFFF, 0x1D6); /* mode 0, division 7 (1/32T) */
    CHECK_EQ(a.seq_index, 0);
    CHECK_EQ(a.div_index, 7);
    app_knobs(&a, 2048, 0x86B, 0xA6E); /* mode 3, division 2 (1/16) */
    CHECK_EQ(a.seq_index, 3);
    CHECK_EQ(a.div_index, 2);
    app_knobs(&a, 2048, 0x7A0, 0xB80); /* both in gaps: unchanged */
    CHECK_EQ(a.seq_index, 3);
    CHECK_EQ(a.div_index, 2);
    /* Shift defers the change until release */
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    app_knobs(&a, 2048, 0x1D6, 0xCDE);
    CHECK_EQ(a.seq_index, 3);
    app_buttons(&a, 0);
    CHECK_EQ(a.seq_index, 7);
    CHECK_EQ(a.div_index, 1);
}

static void test_cmd(uint8_t b7, uint8_t b8, uint8_t idx, uint8_t v)
{
    uint8_t f[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, b7, b8, idx, v, 0xF7};
    sysex(f, sizeof f);
}

/* Shift + Rate: whole BPM, within 10 of the tempo that was set when the knob first moved. */
static void test_fine_tempo(void)
{
    boot();
    app_knobs(&a, 1820, 0xFFF, 0x1D6);
    CHECK_EQ(a.clk.bpm100, 12333);
    app_buttons(&a, 1u << APP_BTN_SHIFT);
    app_knobs(&a, 1900, 0xFFF, 0x1D6); /* first movement only captures 123.00 */
    CHECK_EQ(a.clk.bpm100, 12333);
    app_knobs(&a, 2048, 0xFFF, 0x1D6); /* centre */
    CHECK_EQ(a.clk.bpm100, 12300);
    app_knobs(&a, 2300, 0xFFF, 0x1D6);
    CHECK_EQ(a.clk.bpm100, 12400);
    app_knobs(&a, 4095, 0xFFF, 0x1D6);
    CHECK_EQ(a.clk.bpm100, 13300);
    app_knobs(&a, 0, 0xFFF, 0x1D6);
    CHECK_EQ(a.clk.bpm100, 11300);
    app_buttons(&a, 0);
    app_buttons(&a, 1u << APP_BTN_SHIFT); /* base survives releasing Shift */
    app_knobs(&a, 4095, 0xFFF, 0x1D6);
    CHECK_EQ(a.clk.bpm100, 13300);
    app_buttons(&a, 0);
    app_knobs(&a, 2100, 0xFFF, 0x1D6); /* plain knob: full range again */
    CHECK_EQ(a.clk.bpm100, 13769);
    app_buttons(&a, 1u << APP_BTN_SHIFT); /* new base 137.00 */
    app_knobs(&a, 2200, 0xFFF, 0x1D6);
    app_knobs(&a, 0, 0xFFF, 0x1D6);
    CHECK_EQ(a.clk.bpm100, 12700);
    app_buttons(&a, 0);
}

static void test_factory_mode(void)
{
    boot();
    app_knobs(&a, 2000, 0, 0);
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_PLAYING);
    clear();
    test_cmd(0x7D, 0x7D, 0x09, 0x7F); /* enter */
    CHECK_EQ(a.test_mode, 1);
    CHECK_EQ(a.transport, TR_STOPPED);
    CHECK_EQ(gate_type, 1);
    run_ms(6);
    CHECK_EQ(led_bits, 0);

    /* LEDs by control ID, then all */
    test_cmd(0x00, 0x10, 0x67, 1); /* Tap */
    run_ms(6);
    CHECK_EQ(led_bits, 1u << APP_LED_TAP);
    test_cmd(0x00, 0x10, 0x67, 0);
    test_cmd(0x00, 0x10, 0x11, 1); /* Oct+ */
    run_ms(6);
    CHECK_EQ(led_bits, 1u << APP_LED_OCT_UP);
    test_cmd(0x7D, 0x7D, 0x0B, 1);
    run_ms(6);
    CHECK_EQ(led_bits, 0xFF);
    test_cmd(0x7D, 0x7D, 0x0B, 0);
    run_ms(6);
    CHECK_EQ(led_bits, 0);

    /* buttons report presses and do nothing else */
    clear();
    tap(APP_BTN_PLAY);
    CHECK_EQ(a.transport, TR_STOPPED);
    CHECK(find(0xB0, 0x5A) >= 0);
    CHECK_EQ(out[find(0xB0, 0x5A)].m[2], 1);
    CHECK_EQ(out[find(0xB0, 0x5A)].ports, APP_PORT_USB);
    CHECK_EQ(count(0xB0, 0x5A), 1); /* release not reported */

    /* knobs, switches, pedal, strips, DC jack */
    clear();
    app_knobs(&a, 4095, 0x200, 4095);
    CHECK_EQ(out[find(0xB0, 0x66)].m[2], 127);
    CHECK_EQ(out[find(0xB0, 0x15)].m[2], 8);
    CHECK_EQ(out[find(0xB0, 0x68)].m[2], 1);
    clear();
    app_knobs(&a, 4090, 0x200, 4095); /* within hysteresis */
    CHECK_EQ(count(0xB0, 0x66), 0);
    app_switches(&a, APP_SYNC_IN, 0);
    CHECK_EQ(out[find(0xB0, 0x13)].m[2], 2);
    CHECK_EQ(out[find(0xB0, 0x12)].m[2], 1); /* Seq */
    CHECK_EQ(a.sync_src, APP_SYNC_INTERNAL); /* not applied */
    clear();
    app_switches(&a, APP_SYNC_MIDI, 1);
    CHECK_EQ(out[find(0xB0, 0x13)].m[2], 3);
    CHECK_EQ(out[find(0xB0, 0x12)].m[2], 0);
    clear();
    app_pedal(&a, 4000);
    CHECK(find(0xB0, 0x42) >= 0);
    CHECK_EQ(a.hold_flag, 0);
    app_touch(&a, 1, 0); /* untouched: nothing */
    CHECK_EQ(count(0xB0, 0x40), 0);
    app_touch(&a, 1, 150);
    CHECK(find(0xB0, 0x40) >= 0);
    app_touch(&a, 0, 150);
    CHECK(find(0xB0, 0x41) >= 0);
    clear();
    app_dc_power(&a, 1);
    CHECK_EQ(out[find(0xB0, 0x60)].m[2], 1);
    app_dc_power(&a, 1);
    CHECK_EQ(count(0xB0, 0x60), 1);

    /* keys still play */
    clear();
    app_key(&a, 0, 100);
    CHECK(find(0x90, -1) >= 0);
    app_key(&a, 0, 0);

    /* loopback tests */
    clear();
    test_cmd(0x00, 0x70, 0, 0);
    CHECK(find(0xB5, 0x55) >= 0);
    CHECK_EQ(out[find(0xB5, 0x55)].ports, APP_PORT_DIN);
    clear();
    static const uint8_t mark[3] = {0xB5, 0x55, 0x4A};
    app_midi_in(&a, APP_PORT_DIN, mark, 3);
    CHECK_EQ(out[find(0xB0, 0x02)].m[2], 0x70);
    clear();
    test_cmd(0x00, 0x71, 0, 0);
    CHECK_EQ(hw_test_req, 0x71);
    CHECK_EQ(out[find(0xB0, 0x02)].m[2], 0x71);
    clear();
    test_cmd(0x00, 0x72, 0, 0); /* stub fails this one */
    CHECK_EQ(count(0xB0, 0x02), 0);

    /* CV outputs */
    test_cmd(0x00, 0x73, 0x11, 127);
    CHECK_EQ(last_mod, 0);
    test_cmd(0x00, 0x73, 0x11, 0x60);
    CHECK_EQ(last_mod, 0x60 * 4095 / 127);
    test_cmd(0x00, 0x73, 0x12, 2);
    CHECK_EQ(gate_type, 2);
    CHECK_EQ(gate, 1);
    test_cmd(0x00, 0x73, 0x12, 0);
    CHECK_EQ(gate, 0);
    test_cmd(0x00, 0x73, 0x10, 72);
    uint16_t p72 = last_pitch;
    test_cmd(0x00, 0x73, 0x10, 60);
    CHECK(last_pitch < p72);

    /* leave: settings gate type back, deferred switch positions apply */
    test_cmd(0x7D, 0x7D, 0x09, 0);
    CHECK_EQ(a.test_mode, 0);
    CHECK_EQ(gate_type, settings_get(&a.set, P_GATE_TYPE));
    app_switches(&a, APP_SYNC_MIDI, 1);
    CHECK_EQ(a.sync_src, APP_SYNC_MIDI);
    CHECK_EQ(a.arp_mode, 1);
    tap(APP_BTN_PLAY);
    CHECK(a.transport != TR_STOPPED); /* buttons act again */
}

int main(void)
{
    test_keys();
    test_shift_channel();
    test_chord_mode();
    test_cv_priority();
    test_transport_internal();
    test_transpose();
    test_step_record();
    test_rec_length();
    test_aftertouch();
    test_midi_in();
    test_sync_external();
    test_sysex();
    test_sysex_sequence();
    test_sysex_block_read();
    test_calibration();
    test_leds();
    test_arp();
    test_knobs();
    test_fine_tempo();
    test_arm_to_start();
    test_factory_mode();
    return test_report("test_app");
}
