#include "app.h"

/* ---------------------------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------------------------- */

#define KEY_TOP_NOTE 72 /* keyboard.md 1: note = 72 - k at octave 0, same matrix indexing as stock */

static const uint8_t swing_keys[11] = {50, 53, 55, 57, 59, 61, 64, 67, 70, 73, 75};
static const uint8_t gate_keys[5] = {10, 25, 50, 75, 90};

static uint8_t P(const app_t *a, uint8_t p)
{
    return settings_get(&a->set, p);
}

static void midi3(app_t *a, uint8_t ports, uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t m[3] = {st, d1, d2};
    a->io.midi(a->io.ctx, ports, m, (st & 0xE0) == 0xC0 ? 2 : 3);
}

static void midi1(app_t *a, uint8_t ports, uint8_t st)
{
    a->io.midi(a->io.ctx, ports, &st, 1);
}

static int playing(const app_t *a)
{
    return a->transport == TR_PLAYING;
}

static int shift(const app_t *a)
{
    return (a->buttons >> APP_BTN_SHIFT) & 1u;
}

static int btn(const app_t *a, unsigned b)
{
    return (a->buttons >> b) & 1u;
}

static sequence_t *active_pattern(app_t *a)
{
    return a->arp_mode ? &a->arp.pattern : &a->seq[a->seq_index];
}

static int kbd_play_active(const app_t *a)
{
    return !a->arp_mode && playing(a) && !a->recording && a->kbd_play;
}

/* Channel for keyboard-side controllers: the Kbd Play channel overrides while it is active. */
static uint8_t ctl_channel(const app_t *a, uint8_t p)
{
    return kbd_play_active(a) ? settings_channel(&a->set, P_KBD_PLAY_CH) : settings_channel(&a->set, p);
}

static void seq_touched(app_t *a, sequence_t *s)
{
    if (s >= a->seq && s < a->seq + SEQ_COUNT) {
        s->flags |= SEQ_FLAG_DIRTY;
        a->seq_due_ms = a->now_ms + 10000u;
        if (!a->seq_due_ms)
            a->seq_due_ms = 1;
    }
}

static void settings_touched(app_t *a, uint32_t delay_ms)
{
    a->settings_due_ms = a->now_ms + delay_ms;
    if (!a->settings_due_ms)
        a->settings_due_ms = 1;
}

int app_key_base_note(uint8_t key)
{
    return KEY_TOP_NOTE - (int)key;
}

/* ---------------------------------------------------------------------------------------------
 * CV / gate
 * ------------------------------------------------------------------------------------------- */

static uint16_t mod_code(const app_t *a, uint8_t v)
{
    /* value 0..127 -> DAC code = value/127 * 4095 * max_mV / 12700, as stock. The factory test
     * expects that formula to give 10.0 V at DAC 3095, so the analogue full scale is about
     * 13.2 V and every setting comes out about 4 % high on real hardware (sysex.md 5.3). */
    uint32_t max_v = P(a, P_MOD_CV_MAX);
    uint32_t c = (uint32_t)v * max_v * 40950u / (127u * 127u);
    return (uint16_t)(c > 4095u ? 4095u : c);
}

static void mod_cv(app_t *a, uint8_t source, uint8_t v)
{
    if (P(a, P_MOD_CV_SOURCE) == source)
        a->io.cv_mod(a->io.ctx, mod_code(a, v));
}

static int kbd_pick(const app_t *a)
{
    const note_list_t *l = &a->kbd_list;
    if (!l->n)
        return -1;
    uint8_t pr = P(a, P_NOTE_PRIORITY), best = l->note[l->n - 1];
    if (pr == 2)
        return best;
    for (unsigned i = 0; i < l->n; i++)
        if (pr == 0 ? l->note[i] < best : l->note[i] > best)
            best = l->note[i];
    return best;
}

static uint16_t strip_bend14(const app_t *a)
{
    return a->bend >= 127 ? 0x3FFF : (uint16_t)(a->bend << 7);
}

static void cv_refresh(app_t *a)
{
    int note = -1;
    uint16_t bend = strip_bend14(a);

    if (playing(a)) {
        /* the outputs belong to the sequencer/arp while it plays (midi.md 3) */
        if (a->seq_gate_n)
            note = a->seq_note;
        else
            a->io.cv_pitch(a->io.ctx, pitch_cv_code(&a->cv_cfg, a->seq_note, bend));
    } else if ((note = kbd_pick(a)) < 0 && a->midi_list.n) {
        note = a->midi_list.note[a->midi_list.n - 1];
        bend = a->midi_bend;
    }
    if (note >= 0)
        a->io.cv_pitch(a->io.ctx, pitch_cv_code(&a->cv_cfg, (uint8_t)note, bend));

    uint8_t gate = note >= 0;
    if (gate != a->cv_last_gate) {
        a->cv_last_gate = gate;
        a->io.cv_gate(a->io.ctx, gate);
        if (P(a, P_SYNC_TYPE) == SYNC_OUT_1STEP_GATE && playing(a)) {
            a->sync_clock_level = gate; /* 1 step Gate: Sync Out mirrors the gate */
            a->io.sync_out(a->io.ctx, a->sync_clock_level, a->sync_run_level);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Settings
 * ------------------------------------------------------------------------------------------- */

static void apply_settings(app_t *a)
{
    a->cv_cfg.mode = P(a, P_CV_HZV) ? PITCH_CV_HZV : PITCH_CV_VOCT;
    a->cv_cfg.voct_base = P(a, P_VOCT_BASE);
    a->cv_cfg.hzv_base = P(a, P_HZV_BASE);
    a->cv_cfg.bend_range = P(a, P_BEND_RANGE);
    a->io.gate_type(a->io.ctx, P(a, P_GATE_TYPE));
    a->clk.sync_out = P(a, P_SYNC_TYPE);
    a->clk.tap_avg = (uint8_t)(P(a, P_TAP_AVG) - 1u); /* taps -> intervals */
    a->player.next_mode = P(a, P_NEXT_SEQ);
    a->player.fixed_vel = P(a, P_FIXED_VEL_ON) ? P(a, P_FIXED_VEL) : 0;
    if (a->player.fixed_vel > 127)
        a->player.fixed_vel = 127;
    arp_set_random_variant(&a->arp, P(a, P_ARP_RANDOM));
    cv_refresh(a);
}

/* ---------------------------------------------------------------------------------------------
 * Sequencer / arp note output
 * ------------------------------------------------------------------------------------------- */

static void on_player_note(void *ctx, int on, uint8_t note, uint8_t vel)
{
    app_t *a = ctx;
    uint8_t ch = settings_channel(&a->set, P_USER_CH);
    if (on) {
        midi3(a, APP_PORT_ALL, (uint8_t)(0x90 | ch), note, vel);
        a->seq_note = note;
        a->seq_gate_n++;
        mod_cv(a, 1, vel);
    } else {
        midi3(a, APP_PORT_ALL, (uint8_t)(0x80 | ch), note, 0x40);
        if (a->seq_gate_n)
            a->seq_gate_n--;
    }
    cv_refresh(a);
}

static void update_transpose(app_t *a)
{
    a->player.transpose = (!a->arp_mode && a->transpose >= 0)
                              ? (int8_t)(a->transpose - P(a, P_TRANSPOSE_CENTER))
                              : 0;
}

static void update_chord_link(app_t *a)
{
    /* Arp + chord: every arp step becomes a chord rooted on the arp note (arp_chord.md). */
    if (a->arp_mode && a->chord.state == CHORD_ON) {
        a->player.chord = a->chord.notes;
        a->player.chord_n = a->chord.n;
    } else {
        a->player.chord_n = 0;
    }
}

static void select_pattern(app_t *a)
{
    if (a->arp_mode) {
        a->player.hook = arp_step_hook;
        a->player.hook_ctx = &a->arp;
    } else {
        a->player.hook = 0;
    }
    update_chord_link(a);
    update_transpose(a);
}

/* ---------------------------------------------------------------------------------------------
 * Transport (sequencer.md 2.7, midi.md 4-5)
 * ------------------------------------------------------------------------------------------- */

static int clock_master(const app_t *a)
{
    return a->sync_src == APP_SYNC_INTERNAL || a->sync_src == APP_SYNC_IN;
}

static void send_mmc(app_t *a, uint8_t cmd)
{
    if (!(P(a, P_TRANSPORT_MODE) & 2))
        return;
    uint8_t f[6] = {0xF0, 0x7F, 0x7F, 0x06, cmd, 0xF7};
    /* DIN always; USB unless the DAW is the clock source (no echo). DIN SysEx is sent byte-wise. */
    for (unsigned i = 0; i < 6; i++)
        a->io.midi(a->io.ctx, APP_PORT_DIN, &f[i], 1);
    if (a->sync_src != APP_SYNC_USB)
        a->io.sysex(a->io.ctx, f, 6);
}

static void send_transport_cc(app_t *a, uint8_t ch_p, uint8_t cc_p, uint8_t v)
{
    if (P(a, P_TRANSPORT_MODE) & 1)
        midi3(a, APP_PORT_ALL, (uint8_t)(0xB0 | settings_channel(&a->set, ch_p)), P(a, cc_p) & 0x7F, v);
}

static void set_sync_run(app_t *a, int run)
{
    a->sync_run_level = (uint8_t)run;
    if (!run)
        a->sync_clock_level = 0;
    a->io.sync_out(a->io.ctx, a->sync_clock_level, a->sync_run_level);
}

static void record_set(app_t *a, int on, int append);

static void transport_start(app_t *a)
{
    clock_reset(&a->clk, a->now_us);
    seq_player_set_division(&a->player, a->div_index);
    select_pattern(a);
    a->transport = TR_PLAYING;
    a->seq_gate_n = 0;
    if (a->arp_mode) {
        arp_transport(&a->arp, 1);
        for (unsigned k = 0; k < APP_KEYS; k++) /* keys already down join the arpeggio */
            if (a->key_flags[k] & KEYF_SOUNDING) {
                arp_note_on(&a->arp, a->key_note[k], 100);
                a->key_flags[k] |= KEYF_ARP;
            }
        arp_set_hold(&a->arp, a->hold_flag);
    }
    seq_player_start(&a->player, active_pattern(a));
    if (a->recording)
        rt_rec_reset(&a->rrec);
    if (clock_master(a))
        midi1(a, APP_PORT_ALL, 0xFA);
    set_sync_run(a, 1);
    cv_refresh(a);
}

static void transport_stop(app_t *a)
{
    int was = a->transport;
    seq_player_stop(&a->player);
    arp_transport(&a->arp, 0);
    a->transport = TR_STOPPED;
    a->seq_gate_n = 0;
    if (was != TR_STOPPED && clock_master(a))
        midi1(a, APP_PORT_ALL, 0xFC);
    if (a->recording)
        record_set(a, 0, 0);
    set_sync_run(a, 0);
    /* save edited sequences now that the transport is idle */
    for (unsigned i = 0; i < SEQ_COUNT; i++)
        if (a->seq[i].flags & SEQ_FLAG_DIRTY)
            a->seq_due_ms = a->now_ms ? a->now_ms : 1;
    cv_refresh(a);
}

static void transport_pause(app_t *a)
{
    seq_player_pause(&a->player);
    a->transport = TR_PAUSED;
    a->seq_gate_n = 0;
    if (clock_master(a))
        midi1(a, APP_PORT_ALL, 0xFC);
    set_sync_run(a, 0);
    send_mmc(a, 0x09);
    cv_refresh(a);
}

static void transport_continue(app_t *a)
{
    seq_player_continue(&a->player);
    a->transport = TR_PLAYING;
    if (clock_master(a))
        midi1(a, APP_PORT_ALL, 0xFB);
    set_sync_run(a, 1);
}

static void panic(app_t *a)
{
    for (uint8_t ch = 0; ch < 16; ch++)
        midi3(a, APP_PORT_ALL, (uint8_t)(0xB0 | ch), 123, 0);
    a->cv_last_gate = 0;
    a->io.cv_gate(a->io.ctx, 0);
    a->sync_clock_level = 0;
    a->io.sync_out(a->io.ctx, 0, a->sync_run_level);
}

/* ---------------------------------------------------------------------------------------------
 * Recording (sequencer.md 3)
 * ------------------------------------------------------------------------------------------- */

static void record_set(app_t *a, int on, int append)
{
    if (a->arp_mode)
        on = 0; /* the arp has nothing to record */
    if (on == a->recording)
        return;
    a->recording = (uint8_t)on;
    if (on) {
        a->transpose = -1;
        update_transpose(a);
        step_rec_arm(&a->srec, &a->seq[a->seq_index], P(a, P_TIE_CONV));
        if (append)
            a->srec.started = 1; /* keep the existing steps and add after them */
        rt_rec_reset(&a->rrec);
        a->n_rt_off = 0;
    } else {
        seq_touched(a, &a->seq[a->seq_index]);
    }
    send_transport_cc(a, P_REC_CH, P_REC_CC, on ? 127 : 0);
    send_mmc(a, on ? 0x06 : 0x07);
}

static void rt_record_point(app_t *a)
{
    sequence_t *s = &a->seq[a->seq_index];
    if (a->player.seq != s)
        return;
    rt_rec_write(&a->rrec, s, seq_player_step(&a->player));
    for (unsigned i = 0; i < a->n_rt_off; i++)
        rt_rec_note_off(&a->rrec, a->rt_off[i]);
    a->n_rt_off = 0;
    seq_touched(a, s);
}

/* ---------------------------------------------------------------------------------------------
 * Clock ticks
 * ------------------------------------------------------------------------------------------- */

static void run_tick(app_t *a)
{
    unsigned f = clock_output(&a->clk);

    if ((f & CLOCK_OUT_MIDI_CLOCK) && clock_master(a))
        midi1(a, APP_PORT_ALL, 0xF8);

    if (playing(a)) {
        seq_player_tick(&a->player);
        if (a->recording && a->player.tick_in_step == a->player.T / 2u)
            rt_record_point(a);
        if (a->clk.sync_out != SYNC_OUT_1STEP_GATE && (f & (CLOCK_OUT_SYNC_HIGH | CLOCK_OUT_SYNC_LOW))) {
            a->sync_clock_level = (f & CLOCK_OUT_SYNC_HIGH) ? 1 : 0;
            a->io.sync_out(a->io.ctx, a->sync_clock_level, a->sync_run_level);
        }
    }
    if (f & CLOCK_OUT_BEAT_ON)
        a->beat_led = 1;
    else if (f & CLOCK_OUT_BEAT_OFF)
        a->beat_led = 0;
}

static void run_ticks(app_t *a, unsigned n)
{
    while (n--)
        run_tick(a);
}

static void apply_clock_source(app_t *a)
{
    switch (a->sync_src) {
    case APP_SYNC_USB:
    case APP_SYNC_MIDI:
        clock_set_source(&a->clk, CLOCK_MIDI, CLOCK_TICKS_PER_F8);
        break;
    case APP_SYNC_IN: {
        uint8_t t = P(a, P_SYNC_TYPE);
        clock_set_source(&a->clk, CLOCK_SYNC_IN, t == SYNC_OUT_1STEP_GATE ? 120 : clock_sync_ticks(t));
        break;
    }
    default:
        clock_set_source(&a->clk, CLOCK_INTERNAL, 0);
        a->clk.last_us = a->now_us;
        break;
    }
}

/* ---------------------------------------------------------------------------------------------
 * Hold / sustain controller (keyboard.md 8)
 * ------------------------------------------------------------------------------------------- */

static void hold_update(app_t *a)
{
    uint8_t pf = P(a, P_PEDAL_FUNCTION), bf = P(a, P_HOLD_BTN_FUNCTION);
    uint8_t hold = (a->pedal_on && (pf & 1)) || (a->hold_btn_on && (bf & 1));
    uint8_t sus = (a->pedal_on && (pf & 2)) || (a->hold_btn_on && (bf & 2));

    if (hold != a->hold_flag) {
        a->hold_flag = hold;
        arp_set_hold(&a->arp, hold);
    }
    if (sus != a->sustain_sent) {
        a->sustain_sent = sus;
        uint8_t ch = kbd_play_active(a) ? settings_channel(&a->set, P_KBD_PLAY_CH)
                                        : settings_channel(&a->set, P_SUSTAIN_CH);
        midi3(a, APP_PORT_ALL, (uint8_t)(0xB0 | ch), P(a, P_SUSTAIN_CC) & 0x7F,
              P(a, sus ? P_SUSTAIN_HIGH : P_SUSTAIN_LOW) & 0x7F);
    }
}

static void hold_input(app_t *a, int pedal, int down)
{
    uint8_t *st = pedal ? &a->pedal_on : &a->hold_btn_on;
    uint8_t momentary = P(a, pedal ? P_PEDAL_MODE : P_HOLD_BTN_MODE);
    if (momentary)
        *st = (uint8_t)down;
    else if (down)
        *st ^= 1u;
    hold_update(a);
}

/* ---------------------------------------------------------------------------------------------
 * Keyboard (keyboard.md 1-5)
 * ------------------------------------------------------------------------------------------- */

static void shift_key(app_t *a, uint8_t k)
{
    if (k <= 10) {
        seq_set_swing(active_pattern(a), swing_keys[10 - k]);
        seq_touched(a, active_pattern(a));
    } else if (k <= 15) {
        seq_set_gate(active_pattern(a), gate_keys[15 - k]);
        seq_touched(a, active_pattern(a));
    } else {
        uint8_t ch = (uint8_t)(31 - k);
        if (btn(a, APP_BTN_OCT_UP)) {
            settings_set(&a->set, P_KBD_PLAY_CH, ch);
        } else {
            settings_set(&a->set, P_USER_CH, ch);
            settings_set(&a->set, P_KBD_PLAY_CH, SETTINGS_CH_USER);
        }
        settings_touched(a, 0);
    }
}

static void key_down(app_t *a, uint8_t k, uint8_t vel)
{
    a->key_flags[k] = KEYF_DOWN;
    a->key_n[k] = 0;
    if (a->keys_down++ == 0)
        a->at_block = 2;

    if (shift(a)) {
        a->key_flags[k] |= KEYF_SHIFT;
        shift_key(a, k);
        return;
    }
    int n = app_key_base_note(k) + 12 * a->octave;
    if (n < 0 || n > 127)
        return;
    uint8_t note = (uint8_t)n;
    a->key_note[k] = note;

    if (a->chord.state == CHORD_CAPTURING)
        chord_capture_note(&a->chord, note);

    if (a->arp_mode && playing(a)) {
        arp_note_on(&a->arp, note, vel);
        a->key_flags[k] |= KEYF_ARP;
        return;
    }
    if (!a->arp_mode && playing(a) && !a->recording && !a->kbd_play) {
        a->transpose = note; /* Transpose mode: the key moves the sequence */
        update_transpose(a);
        a->key_flags[k] |= KEYF_TRANSPOSE;
        return;
    }

    uint8_t notes[CHORD_MAX];
    unsigned cnt = 1;
    notes[0] = note;
    if (a->chord.state == CHORD_ON)
        cnt = chord_expand(&a->chord, note, notes);

    if (a->recording) {
        a->key_flags[k] |= KEYF_REC;
        for (unsigned i = 0; i < cnt; i++) {
            if (playing(a))
                rt_rec_note_on(&a->rrec, notes[i], vel);
            else
                step_rec_note_on(&a->srec, notes[i], vel);
        }
    }

    uint8_t ch = kbd_play_active(a) ? settings_channel(&a->set, P_KBD_PLAY_CH)
                                    : settings_channel(&a->set, P_USER_CH);
    a->key_ch[k] = ch;
    for (unsigned i = 0; i < cnt; i++) {
        midi3(a, APP_PORT_ALL, (uint8_t)(0x90 | ch), notes[i], vel);
        a->key_notes[k][i] = notes[i];
    }
    a->key_n[k] = (uint8_t)cnt;
    a->key_flags[k] |= KEYF_SOUNDING;
    note_list_remove(&a->kbd_list, note);
    note_list_add(&a->kbd_list, note, vel);
    mod_cv(a, 1, vel);
    cv_refresh(a);
}

static void key_up(app_t *a, uint8_t k)
{
    uint8_t f = a->key_flags[k];
    a->key_flags[k] = 0;
    if (a->keys_down)
        a->keys_down--;
    if (!(f & KEYF_DOWN) || (f & KEYF_SHIFT))
        return;
    uint8_t note = a->key_note[k];
    if (f & KEYF_ARP)
        arp_note_off(&a->arp, note);
    if (f & KEYF_REC) {
        for (unsigned i = 0; i < a->key_n[k]; i++) {
            if (playing(a)) {
                if (a->n_rt_off < SEQ_STEP_NOTES)
                    a->rt_off[a->n_rt_off++] = a->key_notes[k][i];
            } else {
                step_rec_note_off(&a->srec, a->key_notes[k][i]);
            }
        }
    }
    if (f & KEYF_SOUNDING) {
        for (unsigned i = 0; i < a->key_n[k]; i++)
            midi3(a, APP_PORT_ALL, (uint8_t)(0x80 | a->key_ch[k]), a->key_notes[k][i], 0x40);
        a->key_n[k] = 0;
        note_list_remove(&a->kbd_list, note);
        cv_refresh(a);
    }
}

void app_key(app_t *a, uint8_t key, uint8_t velocity)
{
    if (key >= APP_KEYS)
        return;
    if (velocity)
        key_down(a, key, velocity);
    else
        key_up(a, key);
}

/* ---------------------------------------------------------------------------------------------
 * Knobs and switches
 * ------------------------------------------------------------------------------------------- */

/* Detent knobs (hardware_map.md 4): the pot reads unevenly spaced voltages, position 7 at the
 * low end and 0 at the top. Readings between the windows keep the previous position, which also
 * gives the hysteresis. */
static uint8_t detent(uint8_t prev, uint16_t raw)
{
    static const uint16_t lo[7] = {0x173, 0x317, 0x4AA, 0x647, 0x808, 0xA0B, 0xC7B};
    static const uint16_t hi[7] = {0x239, 0x3DD, 0x570, 0x70D, 0x8CE, 0xAD1, 0xD41};
    if (raw >= 0xF9C)
        return 0;
    for (unsigned i = 0; i < 7; i++)
        if (raw >= lo[i] && raw <= hi[i])
            return (uint8_t)(7u - i);
    return prev;
}

static void apply_mode_knob(app_t *a)
{
    if (a->arp_mode) {
        arp_set_mode(&a->arp, a->knob_mode);
        return;
    }
    if (a->seq_index == a->knob_mode)
        return;
    if (a->recording)
        record_set(a, 0, 0);
    a->seq_index = a->knob_mode;
    a->transpose = -1;
    update_transpose(a);
    if (playing(a) || a->transport == TR_PAUSED)
        seq_player_select(&a->player, &a->seq[a->seq_index]);
    else
        a->player.seq = &a->seq[a->seq_index];
}

/* ---------------------------------------------------------------------------------------------
 * Factory test mode (sysex.md 5.2-5.4). While it is on, the panel controls do nothing and are
 * reported as CC on channel 1 over USB instead; the keys still play.
 * ------------------------------------------------------------------------------------------- */

#define GATE_TYPE_TEST 1 /* V-trig 5 V, as stock sets on entry */

enum { TL_BTN, TL_PITCH, TL_MOD, TL_TEMPO, TL_MODE, TL_DIV, TL_ARP, TL_SYNC, TL_PEDAL, TL_DC };

/* Control IDs used by the test tool, by button index and by LED bit. */
static const uint8_t test_btn_id[8] = {0x55, 0x56, 0x10, 0x11, 0x67, 0x57, 0x59, 0x5A};
static const uint8_t test_led_id[8] = {0x59, 0x5A, 0x55, 0x11, 0x10, 0x57, 0x67, 0x56};

static void test_report(app_t *a, uint8_t id, uint8_t v)
{
    midi3(a, APP_PORT_USB, 0xB0, id & 0x7F, v & 0x7F);
}

/* Report v under id when it differs from the last value in slot. */
static void test_value(app_t *a, unsigned slot, uint8_t id, uint8_t v)
{
    if (a->test_last[slot] == v)
        return;
    a->test_last[slot] = v;
    test_report(a, id, v);
}

static void release(app_t *a, unsigned b);
static void press(app_t *a, unsigned b);

static void test_mode_set(app_t *a, int on)
{
    on = on != 0;
    if (on == a->test_mode)
        return;
    a->test_mode = (uint8_t)on;
    if (on) {
        a->io.gate_type(a->io.ctx, GATE_TYPE_TEST);
        press(a, APP_BTN_STOP); /* stock: a virtual Stop press stops the transport */
        release(a, APP_BTN_STOP);
        a->test_leds = 0;
        a->test_din_wait = 0;
        for (unsigned i = 0; i < sizeof a->test_last; i++)
            a->test_last[i] = 0xFF; /* report every control once */
        a->test_last[TL_BTN] = a->buttons;
    } else {
        /* differs: stock keeps the test gate type until the next settings change */
        a->io.gate_type(a->io.ctx, P(a, P_GATE_TYPE));
    }
}

static void test_command(app_t *a, uint8_t b7, uint8_t b8, uint8_t idx, uint8_t v)
{
    if (b7 == 0x7D && b8 == 0x7D) {
        switch (idx) {
        case 0x09: test_mode_set(a, v > 0x40); break;
        case 0x03: a->test_flags = (uint8_t)((a->test_flags & ~1u) | (v ? 1u : 0u)); break;
        case 0x0A: a->test_flags = (uint8_t)((a->test_flags & ~2u) | (v ? 2u : 0u)); break;
        case 0x0B: a->test_leds = v ? 0xFF : 0x00; break;
        default: break;
        }
        return;
    }
    if (b7 != 0x00)
        return;
    switch (b8) {
    case 0x10: /* LED by control ID */
        for (unsigned i = 0; i < 8; i++)
            if (test_led_id[i] == idx) {
                if (v)
                    a->test_leds |= (uint8_t)(1u << i);
                else
                    a->test_leds &= (uint8_t)~(1u << i);
            }
        break;
    case 0x70: { /* MIDI loop: send a marker on DIN, expect it back on DIN In */
        static const uint8_t mark[3] = {0xB5, 0x55, 0x4A};
        a->io.midi(a->io.ctx, APP_PORT_DIN, mark, 3);
        a->test_din_wait = 1;
        break;
    }
    case 0x71: /* Sync Out clock -> Sync In clock */
    case 0x72: /* Sync Out start/stop -> Sync In start/stop */
    case 0x74: /* pedal input resistance */
        if (a->io.hw_test && a->io.hw_test(a->io.ctx, b8))
            test_report(a, 0x02, b8);
        break;
    case 0x73: /* drive the CV/Gate outputs directly */
        if (idx == 0x10) {
            a->io.cv_pitch(a->io.ctx, pitch_cv_code(&a->cv_cfg, v > 0x7E ? 0 : v, 0x2000));
        } else if (idx == 0x11) {
            a->io.cv_mod(a->io.ctx, v < 0x7F ? (uint16_t)(v * 4095u / 127u) : 0);
        } else if (idx == 0x12) {
            if (v == 1 || v == 2)
                a->io.gate_type(a->io.ctx, v);
            a->io.cv_gate(a->io.ctx, v != 0);
        }
        break;
    default:
        break;
    }
}

void app_dc_power(app_t *a, int on)
{
    a->dc_power = on != 0;
    if (a->test_mode)
        test_value(a, TL_DC, 0x60, a->dc_power);
}

static void apply_div_knob(app_t *a)
{
    a->div_index = a->knob_div; /* stock: the position is the division index (sequencer.md 2.2) */
    seq_player_set_division(&a->player, a->div_index);
}

void app_knobs(app_t *a, uint16_t tempo, uint16_t mode, uint16_t timediv)
{
    uint8_t m = detent(a->pend_mode, mode), d = detent(a->pend_div, timediv);
    a->pend_mode = m;
    a->pend_div = d;
    if (a->test_mode) {
        /* positions apply when the test ends (the next call sees them differ) */
        test_value(a, TL_MODE, 0x15, (uint8_t)(m + 1));
        test_value(a, TL_DIV, 0x68, (uint8_t)(d + 1));
        uint8_t last = a->test_last[TL_TEMPO];
        int centre = last == 0xFF ? -1000 : last * 32 + 16;
        int diff = (int)tempo - centre;
        if (diff < 0)
            diff = -diff;
        if (diff >= 24) /* 7-bit value with a little hysteresis */
            test_value(a, TL_TEMPO, 0x66, (uint8_t)(tempo >> 5));
        return;
    }
    if (!shift(a)) { /* with Shift held the new positions apply on release */
        if (m != a->knob_mode) {
            a->knob_mode = m;
            apply_mode_knob(a);
        }
        if (d != a->knob_div) {
            a->knob_div = d;
            apply_div_knob(a);
        }
    }

    if (a->sync_src != APP_SYNC_INTERNAL)
        return;
    int diff = (int)tempo - (int)a->tempo_raw;
    if (diff < 0)
        diff = -diff;
    if (diff < 12) /* noise */
        return;
    uint32_t bpm = CLOCK_BPM100_MIN + (uint32_t)tempo * (CLOCK_BPM100_MAX - CLOCK_BPM100_MIN) / 4095u;
    uint32_t prev = CLOCK_BPM100_MIN + (uint32_t)a->tempo_raw * (CLOCK_BPM100_MAX - CLOCK_BPM100_MIN) / 4095u;
    a->tempo_raw = tempo;
    if (P(a, P_TEMPO_PICKUP) && !a->tempo_picked) {
        /* pickup: wait until the knob passes the current tempo */
        uint32_t cur = a->clk.bpm100;
        if (!((prev <= cur && bpm >= cur) || (prev >= cur && bpm <= cur)))
            return;
        a->tempo_picked = 1;
    }
    clock_set_bpm100(&a->clk, bpm);
}

void app_switches(app_t *a, uint8_t sync_src, int arp_mode)
{
    if (a->test_mode) {
        /* the test tool numbers the sync positions Int, USB, Sync In, MIDI */
        static const uint8_t code[4] = {[APP_SYNC_INTERNAL] = 0, [APP_SYNC_USB] = 1,
                                        [APP_SYNC_MIDI] = 3, [APP_SYNC_IN] = 2};
        test_value(a, TL_ARP, 0x12, arp_mode ? 0 : 1);
        test_value(a, TL_SYNC, 0x13, code[sync_src & 3]);
        return;
    }
    if (sync_src != a->sync_src) {
        if (a->transport != TR_STOPPED)
            transport_stop(a);
        a->sync_src = sync_src;
        apply_clock_source(a);
    }
    arp_mode = arp_mode != 0;
    if (arp_mode != a->arp_mode) {
        int was = a->transport;
        if (was != TR_STOPPED)
            transport_stop(a);
        if (a->recording)
            record_set(a, 0, 0);
        a->arp_mode = (uint8_t)arp_mode;
        a->transpose = -1;
        apply_mode_knob(a);
        a->player.seq = active_pattern(a);
        select_pattern(a);
        if (was == TR_PLAYING)
            transport_start(a);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Buttons (hardware_map.md, keyboard.md 3, sequencer.md 2.7)
 * ------------------------------------------------------------------------------------------- */

static void press(app_t *a, unsigned b)
{
    int sh = shift(a);
    switch (b) {
    case APP_BTN_HOLD:
        if (sh)
            chord_capture_begin(&a->chord);
        else
            hold_input(a, 0, 1);
        break;

    case APP_BTN_OCT_DOWN:
    case APP_BTN_OCT_UP: {
        int other = btn(a, b == APP_BTN_OCT_UP ? APP_BTN_OCT_DOWN : APP_BTN_OCT_UP);
        if (sh) {
            if (other && !a->arp_mode) {
                seq_clear_steps(&a->seq[a->seq_index]);
                seq_touched(a, &a->seq[a->seq_index]);
            } else if (a->arp_mode) {
                int r = a->arp_range + (b == APP_BTN_OCT_UP ? 1 : -1);
                if (r >= -3 && r <= 3) {
                    a->arp_range = (int8_t)r;
                    arp_set_range(&a->arp, a->arp_range);
                }
            } else {
                a->kbd_play = b == APP_BTN_OCT_UP;
            }
        } else if (other) {
            a->octave = 0;
        } else if (b == APP_BTN_OCT_UP && a->octave < 4) {
            a->octave++;
        } else if (b == APP_BTN_OCT_DOWN && a->octave > -4) {
            a->octave--;
        }
        break;
    }

    case APP_BTN_REC:
        record_set(a, !a->recording, sh);
        break;

    case APP_BTN_TAP:
        if (a->recording && !a->arp_mode) {
            if (playing(a))
                rt_rec_tap(&a->rrec);
            else
                step_rec_tap(&a->srec, 1);
        } else if (a->sync_src == APP_SYNC_INTERNAL) {
            if (clock_tap(&a->clk, a->now_us))
                a->tempo_picked = 0;
        }
        break;

    case APP_BTN_STOP:
        if (sh) {
            if (!a->arp_mode && !playing(a)) {
                if (a->recording)
                    step_rec_delete_last(&a->srec);
                else
                    seq_clear_last(&a->seq[a->seq_index]);
                seq_touched(a, &a->seq[a->seq_index]);
            }
            break;
        }
        send_transport_cc(a, P_STOP_CH, P_STOP_CC, 127);
        send_mmc(a, 0x01);
        transport_stop(a);
        if (!a->stop_presses || a->now_ms - a->stop_window_ms > 600u) {
            a->stop_presses = 1;
            a->stop_window_ms = a->now_ms;
        } else if (++a->stop_presses >= 3) {
            a->stop_presses = 0;
            panic(a);
        }
        break;

    case APP_BTN_PLAY:
        if (sh) {
            if (playing(a))
                seq_player_restart(&a->player);
            break;
        }
        send_transport_cc(a, P_PLAY_CH, P_PLAY_CC, 127);
        switch (a->transport) {
        case TR_STOPPED:
            send_mmc(a, 0x02);
            if (a->sync_src == APP_SYNC_INTERNAL) {
                transport_start(a);
            } else {
                a->transport = TR_ARMED; /* wait for the external start */
                a->armed_from_stop = 1;
            }
            break;
        case TR_ARMED:
            a->transport = TR_STOPPED;
            break;
        case TR_PLAYING:
            if (P(a, P_PLAY_RESETS) && a->sync_src == APP_SYNC_INTERNAL)
                seq_player_restart(&a->player);
            else
                transport_pause(a);
            break;
        case TR_PAUSED:
            send_mmc(a, 0x02);
            transport_continue(a);
            break;
        }
        break;
    }
}

static void release(app_t *a, unsigned b)
{
    switch (b) {
    case APP_BTN_HOLD:
        if (a->chord.state == CHORD_CAPTURING) {
            chord_capture_end(&a->chord);
            update_chord_link(a);
        } else {
            hold_input(a, 0, 0);
        }
        break;
    case APP_BTN_SHIFT:
        if (a->pend_mode != a->knob_mode) {
            a->knob_mode = a->pend_mode;
            apply_mode_knob(a);
        }
        if (a->pend_div != a->knob_div) {
            a->knob_div = a->pend_div;
            apply_div_knob(a);
        }
        break;
    case APP_BTN_TAP:
        if (a->recording && !a->arp_mode && !playing(a)) {
            step_rec_tap(&a->srec, 0);
            seq_touched(a, &a->seq[a->seq_index]);
        }
        break;
    case APP_BTN_STOP:
        send_transport_cc(a, P_STOP_CH, P_STOP_CC, 0);
        break;
    case APP_BTN_PLAY:
        if (!shift(a))
            send_transport_cc(a, P_PLAY_CH, P_PLAY_CC, 0);
        break;
    default:
        break;
    }
}

void app_buttons(app_t *a, uint8_t mask)
{
    if (a->test_mode) {
        uint8_t pressed = mask & (uint8_t)~a->test_last[TL_BTN];
        a->test_last[TL_BTN] = mask;
        for (unsigned b = 0; b < 8; b++)
            if ((pressed >> b) & 1u)
                test_report(a, test_btn_id[b], 1);
        return;
    }
    uint8_t changed = mask ^ a->buttons;
    for (unsigned b = 0; b < 8; b++) {
        if (!((changed >> b) & 1u))
            continue;
        if ((mask >> b) & 1u) {
            a->buttons |= (uint8_t)(1u << b);
            press(a, b);
        } else {
            a->buttons &= (uint8_t)~(1u << b);
            release(a, b);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Pedal, aftertouch, strips
 * ------------------------------------------------------------------------------------------- */

void app_pedal_boot(app_t *a, uint16_t raw)
{
    if (raw < 1000)
        a->pedal_polarity = 0;
    else if (raw > 3095)
        a->pedal_polarity = 1;
}

void app_pedal(app_t *a, uint16_t raw)
{
    int down;
    if (raw < 1000)
        down = a->pedal_polarity != 0;
    else if (raw > 3095)
        down = a->pedal_polarity != 1;
    else
        return;
    if (a->test_mode) {
        test_value(a, TL_PEDAL, 0x42, (uint8_t)down);
        return;
    }
    if (down == a->pedal_state)
        return;
    a->pedal_state = (uint8_t)down;
    if (shift(a) || a->chord.state == CHORD_CAPTURING) { /* Shift + pedal drives chord capture */
        if (down) {
            chord_capture_begin(&a->chord);
        } else if (a->chord.state == CHORD_CAPTURING) {
            chord_capture_end(&a->chord);
            update_chord_link(a);
        }
        return;
    }
    hold_input(a, 1, down);
}

uint8_t app_at_curve(uint8_t curve, uint16_t raw)
{
    uint16_t lo = 256, hi = 2336;
    if (curve == 3) { /* Soft: shorter travel */
        lo = 0x180;
        hi = 0xC40;
        curve = 0;
    }
    if (raw <= lo)
        return 0;
    if (raw >= hi)
        return 127;
    uint32_t t = (uint32_t)(raw - lo) * 255u / (hi - lo); /* 0..254 */
    /* Our own curves, named as in the vendor editor: 0 Lin, 1 Log (eases out, more pressure
     * early), 2 AntiLog (eases in). 3 Soft is Lin over a shorter sensor range (above). */
    switch (curve) {
    case 1: return (uint8_t)((255u - (255u - t) * (255u - t) / 255u) / 2u);
    case 2: return (uint8_t)(t * t / 510u);
    default: return (uint8_t)(t / 2u);
    }
}

void app_aftertouch(app_t *a, uint16_t raw)
{
    /* rise accepted at once, fall only past 32 counts (keyboard.md 6) */
    if (raw > a->at_raw || (unsigned)(a->at_raw - raw) >= 32u)
        a->at_raw = raw;
}

static void aftertouch_period(app_t *a)
{
    uint8_t target = app_at_curve(P(a, P_AT_CURVE), a->at_raw);
    uint8_t v = a->at_value;
    if (target > v)
        v = (uint8_t)(target - v > 3 ? v + 3 : target);
    else if (target < v)
        v = (uint8_t)(v - target > 3 ? v - 3 : target);
    if (a->at_block) {
        a->at_block--;
        return;
    }
    if (v == a->at_value)
        return;
    a->at_value = v;
    if (a->keys_down || v == 0)
        midi3(a, APP_PORT_ALL, (uint8_t)(0xD0 | ctl_channel(a, P_USER_CH)), v, 0);
    mod_cv(a, 2, v);
}

void app_touch(app_t *a, int strip, int raw)
{
    if (raw < 0)
        return;
    a->touch_raw[strip ? 1 : 0] = (uint8_t)raw;
    if (a->test_mode) {
        uint8_t v = touch_scale((uint8_t)raw, a->cal[strip ? 1 : 0]);
        if (v != TOUCH_NONE)
            test_value(a, strip ? TL_MOD : TL_PITCH, strip ? 0x40 : 0x41, v);
        return;
    }
    if (strip == 0) {
        int v = pitch_strip_update(&a->pitch, (uint8_t)raw, a->cal[0]);
        if (v < 0)
            return;
        a->bend = (uint8_t)v;
        uint8_t ch = raw ? settings_channel(&a->set, P_BEND_CH) : ctl_channel(a, P_BEND_CH);
        midi3(a, APP_PORT_ALL, (uint8_t)(0xE0 | ch), v == 127 ? 127 : 0, (uint8_t)v);
        cv_refresh(a);
    } else {
        int v = mod_strip_update(&a->mod, (uint8_t)raw, a->cal[1]);
        if (v < 0)
            return;
        midi3(a, APP_PORT_ALL, (uint8_t)(0xB0 | ctl_channel(a, P_MOD_CH)), P(a, P_MOD_CC) & 0x7F, (uint8_t)v);
        mod_cv(a, 0, (uint8_t)v);
    }
}

/* ---------------------------------------------------------------------------------------------
 * MIDI input (midi.md 2-6)
 * ------------------------------------------------------------------------------------------- */

static uint8_t src_port(const app_t *a)
{
    return a->sync_src == APP_SYNC_USB ? APP_PORT_USB : a->sync_src == APP_SYNC_MIDI ? APP_PORT_DIN : 0;
}

void app_realtime_in(app_t *a, uint8_t port, uint8_t st)
{
    if (st != 0xF8 && st != 0xFA && st != 0xFB && st != 0xFC)
        return;
    if (port != src_port(a)) {
        /* a foreign clock is not mixed into ours; thru only while we are a slave anyway */
        if (port == APP_PORT_DIN && P(a, P_MIDI_THRU) && a->sync_src != APP_SYNC_INTERNAL)
            midi1(a, APP_PORT_DIN, st);
        return;
    }
    /* pass the external clock on: always to DIN, and a DIN clock also to USB */
    midi1(a, port == APP_PORT_DIN ? APP_PORT_ALL : APP_PORT_DIN, st);

    switch (st) {
    case 0xFA:
        transport_start(a);
        break;
    case 0xFB:
        if (a->transport == TR_PAUSED)
            transport_continue(a);
        else if (a->transport != TR_PLAYING)
            transport_start(a);
        break;
    case 0xFC:
        if (a->transport != TR_STOPPED)
            transport_stop(a);
        break;
    default:
        run_ticks(a, clock_ext_pulse(&a->clk, a->now_us));
        break;
    }
}

void app_sync_in_pulse(app_t *a)
{
    if (a->sync_src != APP_SYNC_IN)
        return;
    if (a->transport == TR_ARMED)
        transport_start(a);
    run_ticks(a, clock_ext_pulse(&a->clk, a->now_us));
}

void app_sync_in_run(app_t *a, int run)
{
    run = run != 0;
    if (run == a->sync_in_run)
        return;
    a->sync_in_run = (uint8_t)run;
    /* falling start/stop line ends playback when slaved to Sync In */
    if (!run && a->sync_src == APP_SYNC_IN && a->transport != TR_STOPPED)
        transport_stop(a);
}

static void midi_to_cv(app_t *a, const uint8_t *m, uint8_t len)
{
    uint8_t type = m[0] & 0xF0;
    if ((m[0] & 15) != settings_channel(&a->set, P_MIDI_IN_CH))
        return;
    if ((type == 0x90 || type == 0x80) && len == 3) {
        note_list_remove(&a->midi_list, m[1]);
        if (type == 0x90 && m[2]) {
            note_list_add(&a->midi_list, m[1], m[2]);
            mod_cv(a, 1, m[2]);
        }
    } else if (type == 0xE0 && len == 3) {
        a->midi_bend = (uint16_t)(m[1] | (m[2] << 7));
    } else if (type == 0xD0 && len == 2) {
        mod_cv(a, 2, m[1]);
    } else if (type == 0xB0 && len == 3) {
        switch (m[1]) {
        case 1: mod_cv(a, 0, m[2]); break;
        case 101: a->rpn_msb = m[2]; break;
        case 100: a->rpn_lsb = m[2]; break;
        case 99: case 98: a->rpn_msb = a->rpn_lsb = 0x7F; break;
        case 6:
            if (a->rpn_msb == 0 && a->rpn_lsb == 0) { /* RPN 0: pitch-bend range */
                settings_set(&a->set, P_BEND_RANGE, m[2]);
                apply_settings(a);
                settings_touched(a, 20000);
            }
            break;
        default: break;
        }
    } else {
        return;
    }
    cv_refresh(a);
}

static void ext_transpose(app_t *a, uint8_t port, const uint8_t *m, uint8_t len)
{
    uint8_t type = m[0] & 0xF0, ch = P(a, P_TRANSPOSE_CH);
    if (len != 3 || (type != 0x90 && type != 0x80) || !(P(a, P_TRANSPOSE_PORT) & port))
        return;
    if (ch < 16 && (m[0] & 15) != settings_channel(&a->set, P_TRANSPOSE_CH))
        return;
    if (type == 0x90 && m[2]) {
        if (playing(a) && !a->arp_mode) {
            a->transpose = m[1];
            update_transpose(a);
        }
    } else if (!P(a, P_TRANSPOSE_LATCH) && a->transpose == m[1]) {
        a->transpose = -1;
        update_transpose(a);
    }
}

void app_midi_in(app_t *a, uint8_t port, const uint8_t *m, uint8_t len)
{
    if (!len)
        return;
    if (a->test_mode && port == APP_PORT_DIN && len == 3 && m[0] == 0xB5 && m[1] == 0x55 &&
        m[2] == 0x4A) {
        a->test_din_wait = 0;
        test_report(a, 0x02, 0x70);
        return;
    }
    if (m[0] < 0xF0) {
        midi_to_cv(a, m, len);
        ext_transpose(a, port, m, len);
    }
    if (P(a, P_MIDI_THRU))
        a->io.midi(a->io.ctx, APP_PORT_DIN, m, len);
}

/* ---------------------------------------------------------------------------------------------
 * SysEx (sysex.md)
 * ------------------------------------------------------------------------------------------- */

static void sx_send_arturia(app_t *a, const uint8_t *body, unsigned n)
{
    uint8_t f[APP_SX_MAX] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42};
    unsigned len = 6;
    for (unsigned i = 0; i < n && len < sizeof f - 1; i++)
        f[len++] = body[i];
    f[len++] = 0xF7;
    a->io.sysex(a->io.ctx, f, (uint16_t)len);
}

static void sx_ack(app_t *a)
{
    static const uint8_t b[] = {0x1C, 0x00};
    sx_send_arturia(a, b, sizeof b);
}

/* A parsed Arturia request: cmd b7 b8, index bytes, then a value or a data block (sysex.md 2). */
typedef struct {
    uint8_t cmd, b7, b8;
    uint8_t n_idx, idx[5];
    uint8_t len;          /* block commands: data length */
    const uint8_t *data;  /* block data, or the single value at data[0] */
    uint8_t has_value;
} sx_req_t;

static int sx_parse(const uint8_t *f, unsigned n, sx_req_t *r)
{
    /* f: F0 00 20 6B dev 42 cmd b7 [b8 ...] F7 */
    if (n < 9 || f[5] != 0x42 || (f[4] != 0x7F && f[4] != 0x10))
        return 0;
    const uint8_t *p = f + 6, *end = f + n - 1;
    *r = (sx_req_t){.cmd = p[0], .b7 = p[1]};
    p += 2;
    if (r->cmd == 0x05 || r->cmd == 0x06 || r->cmd == 0x1C)
        return 1;
    if (p >= end)
        return 0;
    r->b8 = *p++;
    if (r->cmd == 0x0B || r->cmd == 0x0C) {
        /* count c, then c + 1 index bytes, then the length, then (write) the data */
        if (p >= end || *p > 3)
            return 0;
        r->n_idx = (uint8_t)(*p++ + 1u);
        if (end - p < r->n_idx + 1)
            return 0;
        for (unsigned i = 0; i < r->n_idx; i++)
            r->idx[i] = *p++;
        r->len = *p++;
        if (r->len > 100)
            r->len = 100;
        if (r->cmd == 0x0C && end - p < r->len)
            return 0;
        r->data = p;
        return 1;
    }
    unsigned extra = 0;
    if (r->cmd >= 0x20 && r->cmd <= 0x25)
        extra = (r->cmd - 0x20u) / 2u + 1u;
    r->n_idx = (uint8_t)(1u + extra);
    if (end - p < r->n_idx)
        return 0;
    for (unsigned i = 0; i < r->n_idx; i++)
        r->idx[i] = *p++;
    r->has_value = r->cmd == 0x02 || r->cmd == 0x21 || r->cmd == 0x23 || r->cmd == 0x25;
    if (r->has_value) {
        if (p >= end)
            return 0;
        r->data = p;
    }
    return 1;
}

/* Sequence addressed by idx[1] (1..8), step by idx[2] (1..64). */
static sequence_t *sx_seq(app_t *a, const sx_req_t *r)
{
    return r->n_idx >= 2 && r->idx[1] >= 1 && r->idx[1] <= SEQ_COUNT ? &a->seq[r->idx[1] - 1] : 0;
}

static int sx_step(const sx_req_t *r, unsigned span)
{
    if (r->n_idx < 3 || r->idx[2] < 1 || r->idx[2] - 1u + span > SEQ_STEPS)
        return -1;
    return r->idx[2] - 1;
}

/* Block read (0x0B -> reply 0x0C). Field codes: 0x60 length, 0x61 swing, 0x62 gate,
 * 0x63 tie flags of 32 steps, 0x65 the 8 velocities of a step, 0x66 its 8 notes. */
static void sx_block_read(app_t *a, const sx_req_t *r, uint8_t *out)
{
    const sequence_t *s = sx_seq(a, r);
    for (unsigned i = 0; i < r->len; i++)
        out[i] = 0;
    if (!s)
        return;
    int st;
    switch (r->b8) {
    case 0x60: out[0] = s->length; break;
    case 0x61: out[0] = s->swing; break;
    case 0x62: out[0] = s->gate; break;
    case 0x63:
        if ((st = sx_step(r, 32)) < 0)
            break;
        for (unsigned i = 0; i < 32 && i < r->len; i++) {
            const seq_slot_t *sl = &s->step[st + i][0];
            out[i] = (sl->vel & SEQ_VEL_TIE_FLAG) && sl->note != SEQ_NOTE_EMPTY;
        }
        break;
    case 0x65:
    case 0x66:
        if ((st = sx_step(r, 1)) < 0)
            break;
        for (unsigned i = 0; i < SEQ_STEP_NOTES && i < r->len; i++) {
            const seq_slot_t *sl = &s->step[st][i];
            if (r->b8 == 0x66)
                out[i] = sl->note & 0x7F;
            else if (sl->note == SEQ_NOTE_EMPTY || sl->note == SEQ_NOTE_REST)
                out[i] = 0;
            else
                out[i] = sl->vel & 0x7F;
        }
        break;
    default: break;
    }
}

/* Block write (0x0C). MCC writes the tie flags, then the velocities, then the notes of each step:
 * a slot whose velocity is 0 becomes empty (slot 0 inside the length becomes a rest). */
static void sx_block_write(app_t *a, const sx_req_t *r)
{
    sequence_t *s = sx_seq(a, r);
    if (!s || !r->len)
        return;
    const uint8_t *d = r->data;
    int st;
    switch (r->b8) {
    case 0x60: s->length = d[0] < 1 ? 1 : d[0] > SEQ_STEPS ? SEQ_STEPS : d[0]; break;
    case 0x61: seq_set_swing(s, d[0]); break;
    case 0x62: seq_set_gate(s, d[0]); break;
    case 0x63:
        if ((st = sx_step(r, 32)) < 0)
            return;
        for (unsigned i = 0; i < 32 && i < r->len; i++)
            s->step[st + i][0].vel = d[i] == 1 ? SEQ_VEL_TIE_FLAG : 0;
        break;
    case 0x65:
        if ((st = sx_step(r, 1)) < 0)
            return;
        s->step[st][0].vel = (uint8_t)((s->step[st][0].vel & SEQ_VEL_TIE_FLAG) | (d[0] & 0x7F));
        for (unsigned i = 1; i < SEQ_STEP_NOTES && i < r->len; i++)
            s->step[st][i].vel = d[i] & 0x7F;
        break;
    case 0x66:
        if ((st = sx_step(r, 1)) < 0)
            return;
        for (unsigned i = 0; i < SEQ_STEP_NOTES && i < r->len; i++) {
            seq_slot_t *sl = &s->step[st][i];
            if (sl->vel & 0x7F)
                sl->note = d[i] & 0x7F;
            else if (i == 0 && st < s->length)
                sl->note = SEQ_NOTE_REST;
            else
                sl->note = SEQ_NOTE_EMPTY;
        }
        break;
    default: return;
    }
    seq_touched(a, s);
}

/* Single value set (0x21 / 0x25): idx[2] step 1..64, idx[3] note slot 1..8. */
static void sx_seq_set(app_t *a, const sx_req_t *r)
{
    sequence_t *s = sx_seq(a, r);
    if (!s)
        return;
    uint8_t v = r->data[0] & 0x7F;
    int st = sx_step(r, 1);
    unsigned slot = r->n_idx >= 4 && r->idx[3] >= 1 && r->idx[3] <= SEQ_STEP_NOTES ? r->idx[3] - 1u : 0;
    switch (r->b8) {
    case 0x60:
        if (v < 1 || v > SEQ_STEPS)
            return;
        for (unsigned i = s->length; i < v; i++) /* steps brought into the loop become rests */
            if (s->step[i][0].note == SEQ_NOTE_EMPTY)
                s->step[i][0].note = SEQ_NOTE_REST;
        s->length = v;
        break;
    case 0x61: seq_set_swing(s, v); break;
    case 0x62: seq_set_gate(s, v); break;
    case 0x63:
        if (st < 0)
            return;
        if (v == 1)
            s->step[st][0].vel |= SEQ_VEL_TIE_FLAG;
        else
            s->step[st][0].vel &= (uint8_t)~SEQ_VEL_TIE_FLAG;
        break;
    case 0x65: { /* velocity; 0 clears the slot */
        if (st < 0 || r->n_idx < 4)
            return;
        seq_slot_t *sl = &s->step[st][slot];
        if (!v)
            sl->note = slot == 0 && st < s->length ? SEQ_NOTE_REST : SEQ_NOTE_EMPTY;
        sl->vel = slot == 0 ? (uint8_t)((sl->vel & SEQ_VEL_TIE_FLAG) | v) : v;
        break;
    }
    case 0x66: { /* note */
        if (st < 0 || r->n_idx < 4)
            return;
        seq_slot_t *sl = &s->step[st][slot];
        /* differs: stock gives an empty slot velocity 0x7F and leaves the note; we also store it */
        if (!(sl->vel & 0x7F))
            sl->vel = (uint8_t)((sl->vel & SEQ_VEL_TIE_FLAG) | 0x7F);
        sl->note = v;
        break;
    }
    default: return;
    }
    seq_touched(a, s);
}

/* Touch-strip calibration (cmd 0x08, b7 0x40): b8 '3' samples the low end, b8 '2' takes the
 * current position as the high end and completes the strip. idx 0x41 = pitch, 0x40 = mod. */
static void sx_calibrate(app_t *a, const sx_req_t *r)
{
    uint8_t which = r->idx[0] & 0x7F;
    if (r->b7 != 0x40 || (which != 0x40 && which != 0x41))
        return;
    unsigned k = which == 0x41 ? 0 : 1;
    if (r->b8 == '3') {
        a->cal_done[k] = 0;
        a->cal_low[k] = a->touch_raw[k];
    } else if (r->b8 == '2') {
        if (a->touch_raw[k]) {
            a->cal[k].hi = a->touch_raw[k];
            a->cal_done[k] = 1;
        }
        if (a->cal_low[k]) {
            a->cal[k].lo = a->cal_low[k];
            a->cal_done[k] = 1;
        }
        if (a->cal_done[0] && a->cal_done[1])
            settings_touched(a, 0); /* the calibration lives on the settings page */
    }
}

/* MCC note preview (sysex.md 5.5): plays the note on the CV voice as if it came in on the
 * MIDI input channel. Velocity 0 releases it. */
static void sx_preview(app_t *a, uint8_t note, uint8_t vel)
{
    uint8_t m[3] = {(uint8_t)(0x90 | settings_channel(&a->set, P_MIDI_IN_CH)), note & 0x7F, vel & 0x7F};
    midi_to_cv(a, m, 3);
}

static void sysex_arturia(app_t *a, const uint8_t *f, unsigned n)
{
    sx_req_t r;
    if (!sx_parse(f, n, &r)) {
        sx_ack(a); /* stock: a malformed frame is answered with an ACK */
        return;
    }
    uint8_t out[4 + 5 + 1 + 100];
    unsigned k = 0;

    switch (r.cmd) {
    case 0x01: /* get one value: global settings at b8 0x41 */
        if (r.b7 == 0x00) {
            uint8_t code = r.idx[0] & 0x7F, v = 0;
            if (r.b8 == 0x41)
                v = settings_get(&a->set, code) & 0x7F;
            if (r.b8 == 0x41 || code) {
                uint8_t rep[] = {0x02, r.b7, r.b8, code, v};
                sx_send_arturia(a, rep, sizeof rep);
            }
        }
        sx_ack(a);
        break;
    case 0x02:
        if (r.b7 == 0x7D && r.b8 == 0x7A && r.idx[0] == 0x06) {
            sx_preview(a, r.data[0], 0x40);
            break;
        }
        if (r.b7 == 0x00 && r.b8 == 0x41 && (r.idx[0] & 0x7F)) {
            settings_set(&a->set, r.idx[0] & 0x7F, r.data[0]);
            apply_settings(a);
            settings_touched(a, 20000);
        }
        if ((r.b7 == 0x7D && r.b8 == 0x7D) || (r.b7 == 0x00 && (r.b8 == 0x10 || (r.b8 >= 0x70 && r.b8 <= 0x74))))
            test_command(a, r.b7, r.b8, r.idx[0], r.data[0]);
        /* not implemented: other b8 < 0x40 controls (no reader known) */
        break;
    case 0x08:
        sx_calibrate(a, &r);
        break;
    case 0x09: { /* meaning unknown; stock answers 0x0A with the value 0x3FFF */
        if (r.b8 == '0' || r.b8 == '2') {
            uint8_t rep[] = {0x0A, r.b7, r.b8, r.idx[0], 0x7F, 0x7F};
            sx_send_arturia(a, rep, sizeof rep);
        }
        break;
    }
    case 0x0B: /* block read -> 0x0C reply */
        out[k++] = 0x0C;
        out[k++] = r.b7;
        out[k++] = r.b8;
        out[k++] = (uint8_t)(r.n_idx - 1u);
        for (unsigned i = 0; i < r.n_idx; i++)
            out[k++] = r.idx[i];
        out[k++] = r.len;
        sx_block_read(a, &r, out + k);
        k += r.len;
        sx_send_arturia(a, out, k);
        sx_ack(a);
        break;
    case 0x0C:
        sx_block_write(a, &r);
        sx_ack(a);
        break;
    case 0x20:
    case 0x22:
    case 0x24:
        /* single-value gets: stock echoes the address without a value, then ACKs */
        out[k++] = r.cmd;
        out[k++] = r.b7;
        out[k++] = r.b8;
        for (unsigned i = 0; i < r.n_idx; i++)
            out[k++] = r.idx[i];
        sx_send_arturia(a, out, k);
        sx_ack(a);
        break;
    case 0x23:
        if (r.b7 == 0x7D && r.b8 == 0x7A && r.idx[0] == 0x06)
            sx_preview(a, r.idx[2], r.data[0]);
        break;
    case 0x21:
    case 0x25:
        sx_seq_set(a, &r);
        sx_ack(a);
        break;
    default: /* 0x0F: not implemented */
        break;
    }
}

static void sysex_frame(app_t *a, const uint8_t *f, unsigned n)
{
    static const uint8_t identity_req[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    static const uint8_t updater[] = {0xF0, 0x5A, 0x57, 0x6E, 0x28, 0x3C, 0x4E, 0x3F};

    if (n == sizeof identity_req) {
        unsigned i;
        for (i = 0; i < n && f[i] == identity_req[i]; i++)
            ;
        if (i == n) {
            /* Report the stock identity so MIDI Control Center recognises the unit. */
            static const uint8_t r[] = {0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x00, 0x20, 0x6B, 0x02,
                                        0x00, 0x08, 0x00, 0x1C, 0x00, 0x01, 0x01, 0xF7};
            a->io.sysex(a->io.ctx, r, sizeof r);
            return;
        }
    }
    if (n >= sizeof updater) {
        unsigned i;
        for (i = 0; i < sizeof updater && f[i] == updater[i]; i++)
            ;
        if (i == sizeof updater) {
            if (a->transport != TR_STOPPED)
                transport_stop(a);
            if (a->io.bootloader)
                a->io.bootloader(a->io.ctx);
            return;
        }
    }
    if (n >= 4 && f[1] == 0x00 && f[2] == 0x20 && f[3] == 0x6B)
        sysex_arturia(a, f, n);
}

void app_sysex_in(app_t *a, uint8_t port, const uint8_t *d, uint8_t len, int last)
{
    if (port != APP_PORT_USB) /* SysEx is handled for USB only */
        return;
    for (unsigned i = 0; i < len; i++) {
        if (d[i] == 0xF0) {
            a->sx_len = 0;
            a->sx_overflow = 0;
        }
        if (a->sx_len < APP_SX_MAX)
            a->sx[a->sx_len++] = d[i];
        else
            a->sx_overflow = 1;
    }
    if (last) {
        if (!a->sx_overflow && a->sx_len >= 2 && a->sx[0] == 0xF0 && a->sx[a->sx_len - 1] == 0xF7)
            sysex_frame(a, a->sx, a->sx_len);
        a->sx_len = 0;
    }
}

/* ---------------------------------------------------------------------------------------------
 * LEDs (keyboard.md 3, 8; sequencer.md 4)
 * ------------------------------------------------------------------------------------------- */

static int blink(const app_t *a, uint32_t period_ms)
{
    return (a->now_ms % period_ms) < period_ms / 2u;
}

static void update_leds(app_t *a)
{
    uint8_t l = 0;
    if (playing(a))
        l |= 1u << APP_LED_PLAY;
    else if (a->transport == TR_PAUSED && blink(a, 1000))
        l |= 1u << APP_LED_PLAY;
    else if (a->transport == TR_ARMED && blink(a, 250))
        l |= 1u << APP_LED_PLAY;
    if (a->transport == TR_STOPPED)
        l |= 1u << APP_LED_STOP;
    if (a->recording)
        l |= 1u << APP_LED_REC;
    if (shift(a))
        l |= 1u << APP_LED_SHIFT;
    if (a->beat_led)
        l |= 1u << APP_LED_TAP;

    if (a->chord.state == CHORD_CAPTURING) {
        if (blink(a, 150))
            l |= 1u << APP_LED_HOLD;
    } else if (a->chord.state == CHORD_ON) {
        if (blink(a, a->hold_flag ? 400 : 1000))
            l |= 1u << APP_LED_HOLD;
    } else if (a->hold_flag) {
        l |= 1u << APP_LED_HOLD;
    }

    /* Octave LEDs: dark at 0, the LED on the shifted side blinks faster for larger shifts.
     * While Shift is held they show the arp octave range (Arp) or the keyboard mode (Seq:
     * Oct- = Transpose, Oct+ = Kbd Play; keyboard.md 6). */
    static const uint16_t oct_period[5] = {0, 1200, 800, 400, 200};
    int8_t o = shift(a) && a->arp_mode ? a->arp_range : a->octave;
    if (shift(a) && !a->arp_mode) {
        l |= 1u << (a->kbd_play ? APP_LED_OCT_UP : APP_LED_OCT_DOWN);
    } else if (o != 0) {
        unsigned m = (unsigned)(o < 0 ? -o : o);
        if (blink(a, oct_period[m > 4 ? 4 : m]))
            l |= 1u << (o > 0 ? APP_LED_OCT_UP : APP_LED_OCT_DOWN);
    }
    if (a->test_mode)
        l = a->test_leds;
    a->io.leds(a->io.ctx, l, P(a, P_LED_BRIGHTNESS));
}

/* ---------------------------------------------------------------------------------------------
 * Poll, save, init
 * ------------------------------------------------------------------------------------------- */

void app_poll(app_t *a, uint32_t now_us)
{
    uint32_t dt = now_us - a->now_us;
    a->now_us = now_us;
    a->ms_frac_us += dt;
    a->now_ms += a->ms_frac_us / 1000u;
    a->ms_frac_us %= 1000u;

    run_ticks(a, clock_poll(&a->clk, now_us));

    if (a->now_ms - a->at_ms >= 2u) {
        a->at_ms = a->now_ms;
        aftertouch_period(a);
    }
    if (a->now_ms - a->led_ms >= 5u) {
        a->led_ms = a->now_ms;
        update_leds(a);
    }
}

static int due(const app_t *a, uint32_t t)
{
    return t && (int32_t)(a->now_ms - t) >= 0;
}

uint16_t app_save_request(app_t *a)
{
    /* A page erase stops the CPU for 20-40 ms, so saves start only while the transport is idle.
     * differs: the stock firmware also starts them while playing. */
    if (a->transport == TR_PLAYING || a->recording)
        return 0;
    uint16_t m = 0;
    if (due(a, a->seq_due_ms))
        for (unsigned i = 0; i < SEQ_COUNT; i++)
            if (a->seq[i].flags & SEQ_FLAG_DIRTY)
                m |= (uint16_t)APP_SAVE_SEQ(i);
    if (due(a, a->settings_due_ms))
        m |= APP_SAVE_SETTINGS;
    return m;
}

void app_save_done(app_t *a, uint16_t mask)
{
    for (unsigned i = 0; i < SEQ_COUNT; i++)
        if (mask & APP_SAVE_SEQ(i))
            a->seq[i].flags &= (uint8_t)~SEQ_FLAG_DIRTY;
    if (mask & 0xFFu) {
        a->seq_due_ms = 0;
        for (unsigned i = 0; i < SEQ_COUNT; i++)
            if (a->seq[i].flags & SEQ_FLAG_DIRTY)
                a->seq_due_ms = a->now_ms ? a->now_ms : 1;
    }
    if (mask & APP_SAVE_SETTINGS) {
        a->settings_due_ms = 0;
        a->set.dirty = 0;
    }
}

void app_init(app_t *a, const app_io_t *io, const uint8_t *settings_img, const uint8_t *cal_img,
              const uint8_t *const seq_img[SEQ_COUNT])
{
    *a = (app_t){0};
    a->io = *io;
    settings_load(&a->set, settings_img);

    if (cal_img[0] == 0xFF) {
        a->cal[0] = a->cal[1] = (touch_cal_t){.lo = 0x09, .hi = 0xBF};
    } else {
        a->cal[0] = (touch_cal_t){.hi = cal_img[0], .lo = cal_img[1]};
        a->cal[1] = (touch_cal_t){.hi = cal_img[2], .lo = cal_img[3]};
    }

    for (unsigned i = 0; i < SEQ_COUNT; i++) {
        const uint8_t *img = seq_img ? seq_img[i] : 0;
        if (img && !(img[0x400] == 0xFF && img[0] == 0xFF)) {
            uint8_t *dst = (uint8_t *)&a->seq[i];
            for (unsigned j = 0; j < sizeof(sequence_t); j++)
                dst[j] = img[j];
            sequence_t *s = &a->seq[i];
            if (s->length < 1 || s->length > SEQ_STEPS)
                s->length = 1;
            if (s->swing < 50 || s->swing > 75)
                s->swing = 50;
            if (s->gate < 1 || s->gate > 99)
                s->gate = 50;
            s->flags &= (uint8_t)~SEQ_FLAG_DIRTY;
        } else {
            seq_init_empty(&a->seq[i], P(a, P_TIE_CONV));
        }
    }

    pitch_cv_config_default(&a->cv_cfg);
    seq_player_init(&a->player, on_player_note, a);
    arp_init(&a->arp, &a->player);
    seq_set_swing(&a->arp.pattern, 50);
    seq_set_gate(&a->arp.pattern, 50);
    chord_init(&a->chord);
    clock_init(&a->clk);
    pitch_strip_init(&a->pitch);
    mod_strip_init(&a->mod);
    a->bend = 64;
    a->midi_bend = 0x2000;
    a->rpn_msb = a->rpn_lsb = 0x7F;
    a->transpose = -1;
    a->ext_transpose = -1;
    a->knob_div = a->pend_div = 2; /* 1/16 until the knob is read */
    a->div_index = 2;
    a->tempo_raw = 0xFFFF;         /* first knob reading sets the tempo */
    a->tempo_picked = 1;
    a->player.seq = &a->seq[0];
    a->player.hook = 0;
    seq_player_set_division(&a->player, a->div_index);
    apply_settings(a);
    a->io.cv_gate(a->io.ctx, 0);
    set_sync_run(a, 0);
}
