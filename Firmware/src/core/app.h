/* Application layer: the KeyStep behaviour on top of the core modules.
 *
 * Hardware independent. The firmware main loop (src/main.c) feeds it inputs (keys, buttons,
 * knobs, touch strips, MIDI, sync pulses, time) and it drives the outputs through app_io_t, so
 * the whole UI can be exercised by host tests.
 *
 * Behaviour reference: Docs/re/keyboard.md, sequencer.md, arp_chord.md, midi.md, sysex.md,
 * settings.md. Deliberate differences from the stock firmware are marked "differs:". */
#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "settings.h"
#include "sequence.h"
#include "seq_player.h"
#include "arp.h"
#include "chord.h"
#include "clock.h"
#include "touch_strip.h"
#include "pitch_cv.h"

#define APP_PORT_USB 1u
#define APP_PORT_DIN 2u
#define APP_PORT_ALL 3u

/* Buttons, same bit order as hw/panel.h. */
enum app_button {
    APP_BTN_HOLD, APP_BTN_SHIFT, APP_BTN_OCT_DOWN, APP_BTN_OCT_UP,
    APP_BTN_TAP, APP_BTN_REC, APP_BTN_STOP, APP_BTN_PLAY,
};

/* LEDs, same bit order as hw/leds.h. */
enum app_led {
    APP_LED_STOP, APP_LED_PLAY, APP_LED_HOLD, APP_LED_OCT_UP,
    APP_LED_OCT_DOWN, APP_LED_REC, APP_LED_TAP, APP_LED_SHIFT,
};

/* Rear sync switch, same order as hw/panel.h. */
enum app_sync { APP_SYNC_INTERNAL, APP_SYNC_USB, APP_SYNC_MIDI, APP_SYNC_IN };

/* TR_FOLLOW: Arm to Start, the external clock runs and the position follows it, but nothing plays. */
enum app_transport { TR_STOPPED, TR_ARMED, TR_PLAYING, TR_PAUSED, TR_FOLLOW };

/* Save requests (bit = item). */
#define APP_SAVE_SEQ(n)   (1u << (n))
#define APP_SAVE_SETTINGS (1u << 8) /* settings + touch calibration share one page */

typedef struct {
    void *ctx;
    void (*midi)(void *ctx, uint8_t ports, const uint8_t *msg, uint8_t len); /* one short message */
    void (*sysex)(void *ctx, const uint8_t *data, uint16_t len);             /* F0..F7, USB only */
    void (*cv_pitch)(void *ctx, uint16_t code);
    void (*cv_mod)(void *ctx, uint16_t value12);
    void (*cv_gate)(void *ctx, int on);
    void (*gate_type)(void *ctx, uint8_t type);
    void (*sync_out)(void *ctx, int clock, int run);
    void (*leds)(void *ctx, uint8_t bits, uint8_t brightness);
    void (*bootloader)(void *ctx); /* write the updater flag and reset (may be 0) */
    int (*hw_test)(void *ctx, uint8_t test); /* factory loopback 0x71/0x72/0x74: 1 = pass (may be 0) */
} app_io_t;

#define APP_KEYS 32
#define APP_SX_MAX 128

typedef struct {
    app_io_t io;
    settings_t set;
    touch_cal_t cal[2];   /* pitch, mod */
    pitch_cv_config_t cv_cfg;

    sequence_t seq[SEQ_COUNT];
    seq_player_t player;
    arp_t arp;
    chord_t chord;
    mclock_t clk;
    step_rec_t srec;
    rt_rec_t rrec;
    uint8_t rt_off[SEQ_STEP_NOTES], n_rt_off; /* releases applied after the next step write */

    /* panel state */
    uint8_t buttons;
    uint8_t arp_mode;     /* mode switch: 1 = Arp, 0 = Seq */
    uint8_t sync_src;     /* enum app_sync */
    uint8_t seq_index;    /* 0..7 */
    uint8_t knob_mode, knob_div; /* detent 0..7 */
    uint8_t pend_mode, pend_div; /* positions to apply on Shift release */
    uint8_t cand_mode, cand_div, cnt_mode, cnt_div; /* detent debounce: position being timed */
    uint8_t div_index;    /* seq_division_ticks() index */
    int8_t octave;        /* -4..+4 */
    int8_t arp_range;     /* -3..+3 */
    uint8_t kbd_play;     /* seq mode: 0 Transpose, 1 Kbd Play */

    /* keyboard */
    uint8_t key_notes[APP_KEYS][CHORD_MAX]; /* notes sounding for each key */
    uint8_t key_n[APP_KEYS];
    uint8_t key_ch[APP_KEYS];
    uint8_t key_note[APP_KEYS];             /* note the key produced (before chord) */
    uint8_t key_flags[APP_KEYS];            /* KEYF_* */
    uint8_t keys_down;
    note_list_t kbd_list;                   /* keyboard notes in press order (CV priority) */

    /* transport and recording */
    uint8_t transport;    /* enum app_transport */
    uint8_t armed_from_stop;
    uint8_t follow_blink; /* TR_FOLLOW entered with Play: Play LED blinks, else it looks stopped */
    uint8_t recording;
    uint8_t rec_shift;    /* Shift was down when Rec was pressed */
    uint8_t rec_len;      /* length entered with Rec held + channel keys, 0 = none */
    uint8_t stop_presses;
    uint32_t stop_window_ms;
    int16_t transpose;    /* note, -1 = none */
    int16_t ext_transpose;

    /* hold / sustain controller (keyboard.md section 8) */
    uint8_t pedal_on, hold_btn_on, hold_flag, sustain_sent;
    uint8_t pedal_polarity, pedal_state;

    /* strips and aftertouch */
    pitch_strip_t pitch;
    mod_strip_t mod;
    uint8_t bend;         /* 0..127, 64 = centre */
    uint8_t touch_raw[2];  /* last raw strip positions (calibration) */
    uint8_t cal_low[2], cal_done[2];
    uint16_t at_raw;
    uint8_t at_value, at_block;

    /* tempo knob */
    uint16_t tempo_raw;
    uint8_t tempo_picked; /* pickup mode: knob has caught the current tempo */
    uint8_t tempo_fine;   /* Shift + Rate: base tempo captured */
    uint16_t tempo_base;  /* whole BPM x 100 that Shift + Rate moves around */

    /* CV ownership */
    uint8_t seq_note, seq_gate_n;           /* last player note, sounding count */
    note_list_t midi_list;                  /* MIDI-to-CV notes in arrival order */
    uint16_t midi_bend;
    uint8_t rpn_msb, rpn_lsb;
    uint8_t cv_last_gate;

    /* outputs */
    uint8_t sync_clock_level, sync_run_level;
    uint8_t sync_in_run;
    uint8_t beat_led;

    /* SysEx input */
    uint8_t sx[APP_SX_MAX];
    uint16_t sx_len;
    uint8_t sx_overflow;

    /* saving */
    uint16_t save_mask;
    uint32_t settings_due_ms; /* 0 = none */
    uint32_t seq_due_ms;

    /* factory test mode (sysex.md 5.2-5.4) */
    uint8_t test_mode, test_flags, test_leds, test_din_wait;
    uint8_t test_last[10]; /* last reported input values, enum in app.c */
    uint8_t dc_power;

    uint32_t now_us, now_ms;  /* now_ms counts from boot, wraps after 49 days */
    uint32_t ms_frac_us;
    uint32_t at_ms, led_ms;
} app_t;

/* key flags */
#define KEYF_DOWN     0x01
#define KEYF_SOUNDING 0x02
#define KEYF_SHIFT    0x04 /* consumed by a Shift function: no note-off */
#define KEYF_ARP      0x08
#define KEYF_TRANSPOSE 0x10
#define KEYF_REC      0x20

/* Boot. settings_img: 0x80 bytes at 0x0803F000. cal_img: 4 bytes at 0x0803F200.
 * seq_img[n]: 0x408 bytes at 0x0803B000 + n*0x800 (an erased page gives an empty sequence). */
void app_init(app_t *a, const app_io_t *io, const uint8_t *settings_img, const uint8_t *cal_img,
              const uint8_t *const seq_img[SEQ_COUNT]);

/* Inputs */
void app_key(app_t *a, uint8_t key, uint8_t velocity); /* key 0 = top key, velocity 0 = release */
void app_buttons(app_t *a, uint8_t pressed_mask);      /* full button state, bit = enum app_button */
void app_switches(app_t *a, uint8_t sync_src, int arp_mode);
void app_knobs(app_t *a, uint16_t tempo, uint16_t mode, uint16_t timediv); /* raw 12-bit */
void app_pedal(app_t *a, uint16_t raw);
void app_aftertouch(app_t *a, uint16_t raw);
void app_touch(app_t *a, int strip, int raw);          /* strip 0 pitch, 1 mod; raw < 0 = no data */
void app_midi_in(app_t *a, uint8_t port, const uint8_t *msg, uint8_t len);
void app_realtime_in(app_t *a, uint8_t port, uint8_t status);
void app_sysex_in(app_t *a, uint8_t port, const uint8_t *data, uint8_t len, int last);
void app_sync_in_pulse(app_t *a);                      /* Sync In clock rising edge */
void app_sync_in_run(app_t *a, int run);               /* Sync In start/stop level */
void app_dc_power(app_t *a, int on);                   /* DC jack detect (PD4) */
void app_poll(app_t *a, uint32_t now_us);               /* clock, timers, LEDs: call often */

/* Saving: the caller writes the item, then clears its bit. */
uint16_t app_save_request(app_t *a);
void app_save_done(app_t *a, uint16_t mask);

/* Pedal polarity sensing at power-up (keyboard.md section 7). */
void app_pedal_boot(app_t *a, uint16_t raw);

/* Velocity curve for aftertouch: raw 12-bit sensor -> 0..127 (before slew). */
uint8_t app_at_curve(uint8_t curve, uint16_t raw);

/* Key index (0 = top) -> MIDI note at octave 0. */
int app_key_base_note(uint8_t key);

#endif
