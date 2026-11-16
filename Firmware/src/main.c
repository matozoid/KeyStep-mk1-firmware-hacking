/* KeyStep Mk1 open firmware: hardware glue around the application layer (core/app.c).
 *
 * The main loop polls every input, feeds it to the app, and performs the flash writes the app
 * asks for, in the background (save_tick). All behaviour lives in core/app.c. */
#include <stdint.h>
#include "hw/system.h"
#include "hw/panel.h"
#include "hw/cv.h"
#include "hw/leds.h"
#include "hw/analog.h"
#include "hw/flash.h"
#include "hw/midi_uart.h"
#include "hw/usb_midi.h"
#include "hw/touch.h"
#include "hw/sync.h"
#include "core/app.h"
#include "core/keyscan.h"
#include "core/midi_parser.h"
#include "core/usb_midi_packet.h"
#include "core/led_level.h"

#define SEQ_BASE      0x0803B000u
#define SETTINGS_PAGE 0x0803F000u
#define CAL_OFFSET    0x200u
#define BOOT_FLAG     0x0803F800u

static app_t app;
static keyscan_t keys;
static midi_parser_t din_parser, usb_parser;
static usb_midi_sysex_enc_t sx_enc;
static led_level_t led_lvl;
static uint8_t led_setting = 100;

/* ---- app outputs ---- */

static uint8_t clock_sent; /* a MIDI clock byte went out during this millisecond */

static void io_midi(void *ctx, uint8_t ports, const uint8_t *msg, uint8_t len)
{
    (void)ctx;
    if (msg[0] == 0xF8)
        clock_sent = 1;
    if (ports & APP_PORT_DIN)
        midi_uart_send(msg, len);
    if ((ports & APP_PORT_USB) && msg[0] != 0xF0 && msg[0] != 0xF7 && msg[0] >= 0x80)
        usb_midi_send(msg, len);
}

static void usb_packet(void *ctx, const uint8_t pkt[4])
{
    (void)ctx;
    usb_midi_send_packet(pkt);
}

static void io_sysex(void *ctx, const uint8_t *d, uint16_t len)
{
    (void)ctx;
    for (uint16_t i = 0; i < len; i++)
        usb_midi_sysex_feed(&sx_enc, 0, d[i], usb_packet, 0);
}

static void io_cv_pitch(void *ctx, uint16_t code) { (void)ctx; cv_pitch(code); }
static void io_cv_mod(void *ctx, uint16_t v) { (void)ctx; cv_mod(v); }
static void io_cv_gate(void *ctx, int on) { (void)ctx; cv_gate(on); }
static void io_gate_type(void *ctx, uint8_t t) { (void)ctx; cv_set_gate_type((enum gate_type)t); }
static void io_sync_out(void *ctx, int clock, int run) { (void)ctx; sync_out(clock, run); }

static void io_leds(void *ctx, uint8_t bits, uint8_t brightness)
{
    (void)ctx;
    leds_write(bits);
    led_setting = brightness; /* applied by led_brightness_tick() */
}

/* 1 ms: PD4 level and fades (core/led_level.c) */
static void led_brightness_tick(void)
{
    static uint8_t last = 0xFF;
    uint8_t pct = led_level_tick(&led_lvl, panel_led_full(), led_setting);
    if (pct != last) {
        last = pct;
        cv_led_brightness(pct);
    }
}

/* Power-up light show (as stock): each LED in turn fades in and out, then all LEDs pulse once.
 * The factory-reset variant walks Hold, Oct-, Oct+, Shift twice. */
static void led_ramp(unsigned from, unsigned to, unsigned ms)
{
    for (unsigned t = 0; t < ms; t++) {
        cv_led_brightness((uint8_t)(from + ((int)to - (int)from) * (int)t / (int)ms));
        delay_ms(1);
    }
    cv_led_brightness((uint8_t)to);
}

static void boot_animation(int factory)
{
    static const uint8_t order[8] = {LED_PLAY, LED_STOP, LED_REC, LED_TAP,
                                     LED_HOLD, LED_OCT_DOWN, LED_OCT_UP, LED_SHIFT};
    for (unsigned i = 0; i < 8; i++) {
        leds_write((uint8_t)(1u << order[factory ? 4 + (i & 3) : i]));
        led_ramp(0, 30, 60);
        led_ramp(30, 0, 60);
    }
    leds_write(0xFF);
    led_ramp(0, 20, 200);
    led_ramp(20, 0, 200);
    leds_write(0);
}

static int seq_flash_empty(void)
{
    for (unsigned i = 0; i < SEQ_COUNT; i++)
        if (*(const uint32_t *)(SEQ_BASE + i * FLASH_PAGE_SIZE) != 0xFFFFFFFFu)
            return 0;
    return 1;
}

static void enter_bootloader(void)
{
    static const uint32_t flag[2] = {0xA33A95FFu, 0xAAA55A55u};
    flash_erase_page(BOOT_FLAG);
    flash_program(BOOT_FLAG, flag, sizeof flag);
    system_reset();
}

static void io_bootloader(void *ctx)
{
    (void)ctx;
    enter_bootloader();
}

/* Factory loopback tests (sysex.md 5.2). 0x74: with the test plug in the pedal jack the
 * average of 16 readings must lie in 0x971..0x997. */
static int io_hw_test(void *ctx, uint8_t test)
{
    (void)ctx;
    if (test == 0x71 || test == 0x72)
        return sync_loop_test(test == 0x72);
    if (test == 0x74) {
        uint32_t sum = 0;
        for (int i = 0; i < 16; i++) {
            sum += analog_read(ANALOG_PEDAL);
            uint32_t t = micros();
            while (micros() - t < 200u) {
            }
        }
        sum /= 16u;
        return sum >= 0x971u && sum <= 0x997u;
    }
    return 0;
}

static const app_io_t io = {
    .midi = io_midi,
    .sysex = io_sysex,
    .cv_pitch = io_cv_pitch,
    .cv_mod = io_cv_mod,
    .cv_gate = io_cv_gate,
    .gate_type = io_gate_type,
    .sync_out = io_sync_out,
    .leds = io_leds,
    .bootloader = io_bootloader,
    .hw_test = io_hw_test,
};

/* ---- MIDI inputs ---- */

static void din_msg(void *ctx, const uint8_t *m, uint8_t len) { (void)ctx; app_midi_in(&app, APP_PORT_DIN, m, len); }
static void din_rt(void *ctx, uint8_t s) { (void)ctx; app_realtime_in(&app, APP_PORT_DIN, s); }
static void usb_msg(void *ctx, const uint8_t *m, uint8_t len) { (void)ctx; app_midi_in(&app, APP_PORT_USB, m, len); }
static void usb_rt(void *ctx, uint8_t s) { (void)ctx; app_realtime_in(&app, APP_PORT_USB, s); }
static void usb_sx(void *ctx, const uint8_t *d, uint8_t len, int last)
{
    (void)ctx;
    app_sysex_in(&app, APP_PORT_USB, d, len, last);
}

static const midi_parser_cb_t din_cb = {0, din_msg, din_rt, 0};
static const midi_parser_cb_t usb_cb = {0, usb_msg, usb_rt, usb_sx};

static void on_key(void *ctx, uint8_t key, uint8_t velocity)
{
    (void)ctx;
    app_key(&app, key, velocity);
}

/* ---- flash ---- */

static void factory_reset(void)
{
    /* Sequences become empty (differs: the stock firmware writes demo sequences); settings
     * return to defaults; the touch calibration at +0x200 is kept. */
    static uint8_t cal[4];
    const uint8_t *page = (const uint8_t *)SETTINGS_PAGE;
    for (unsigned i = 0; i < 4; i++)
        cal[i] = page[CAL_OFFSET + i];
    /* write empty placeholders so the pages are not blank (a blank flash triggers this reset) */
    static sequence_t empty;
    seq_init_empty(&empty, 0);
    for (unsigned i = 0; i < SEQ_COUNT; i++) {
        flash_erase_page(SEQ_BASE + i * FLASH_PAGE_SIZE);
        flash_program(SEQ_BASE + i * FLASH_PAGE_SIZE, &empty, sizeof empty);
    }
    flash_erase_page(SETTINGS_PAGE);
    if (cal[0] != 0xFF)
        flash_program(SETTINGS_PAGE + CAL_OFFSET, cal, sizeof cal);
}

/* Background save, one flash page at a time, called every 1 ms.
 *
 * A page erase stops everything for 20-40 ms (hw/flash.c), so it starts right after a MIDI clock
 * byte has gone out: the next one is then late by a few ms at most instead of a whole erase. The
 * page is then programmed a few half-words per call (about 50 us each), which takes 130 ms for a
 * sequence. The app only asks for saves while the transport is idle; programming that has already
 * started carries on if playback begins. */
#define SAVE_HALFWORDS_PER_MS 4u
#define SAVE_CLOCK_WAIT_MS    100u /* erase anyway when no clock is being sent */
#define SAVE_IMG_SIZE         (sizeof(sequence_t) > CAL_OFFSET + 4u ? sizeof(sequence_t) : CAL_OFFSET + 4u)

static void save_tick(void)
{
    static union {
        uint8_t b[SAVE_IMG_SIZE];
        sequence_t seq;
    } img;
    static uint32_t addr, pos, len;
    static uint8_t wait;

    if (pos < len) {
        for (unsigned n = 0; n < SAVE_HALFWORDS_PER_MS && pos < len; pos += 2) {
            if (img.b[pos] == 0xFF && img.b[pos + 1] == 0xFF)
                continue; /* same as erased */
            flash_program(addr + pos, &img.b[pos], 2);
            n++;
        }
        return;
    }

    uint16_t m = app_save_request(&app);
    if (!m) {
        wait = 0;
        return;
    }
    if (!clock_sent && ++wait < SAVE_CLOCK_WAIT_MS)
        return;
    wait = 0;

    /* take the snapshot now and clear the request, so that a later edit asks for a new save */
    uint16_t item = APP_SAVE_SETTINGS;
    for (unsigned i = 0; i < SEQ_COUNT; i++) {
        if (m & APP_SAVE_SEQ(i)) {
            item = (uint16_t)APP_SAVE_SEQ(i);
            img.seq = app.seq[i];
            img.seq.flags &= (uint8_t)~SEQ_FLAG_DIRTY;
            addr = SEQ_BASE + i * FLASH_PAGE_SIZE;
            len = sizeof(sequence_t);
            break;
        }
    }
    if (item == APP_SAVE_SETTINGS) {
        for (unsigned i = 0; i < CAL_OFFSET; i++)
            img.b[i] = i < SETTINGS_SIZE ? app.set.v[i] : 0xFF;
        img.b[CAL_OFFSET + 0] = app.cal[0].hi;
        img.b[CAL_OFFSET + 1] = app.cal[0].lo;
        img.b[CAL_OFFSET + 2] = app.cal[1].hi;
        img.b[CAL_OFFSET + 3] = app.cal[1].lo;
        addr = SETTINGS_PAGE;
        len = CAL_OFFSET + 4u;
    }
    app_save_done(&app, item);
    pos = 0;
    flash_erase_page(addr);
}

/* Buttons are scanned every 1 ms; a change counts once the raw mask has been the same for
 * DEBOUNCE_MS scans in a row. */
#define DEBOUNCE_MS 5u

static uint8_t btn_stable;

static uint8_t debounce_buttons(uint8_t raw)
{
    static uint8_t candidate, count;
    if (raw != candidate) {
        candidate = raw;
        count = 0;
    } else if (count < DEBOUNCE_MS && ++count == DEBOUNCE_MS) {
        btn_stable = raw;
    }
    return btn_stable;
}

/* ---- main ---- */

int main(void)
{
    panel_init();
    cv_init();
    leds_init();
    analog_init();
    midi_uart_init();
    usb_midi_init();
    sync_init();
    touch_init();
    keyscan_init(&keys);
    midi_parser_init(&din_parser);
    midi_parser_init(&usb_parser);

    delay_ms(20); /* let the ADC fill and the buttons settle */
    uint8_t b = panel_scan_buttons();
    const uint8_t boot_combo = (1u << BTN_REC) | (1u << BTN_STOP) | (1u << BTN_PLAY);
    if ((b & boot_combo) == boot_combo)
        enter_bootloader();
    boot_animation(0);
    if (((b & (1u << BTN_OCT_DOWN)) && (b & (1u << BTN_OCT_UP))) || seq_flash_empty()) {
        boot_animation(1);
        factory_reset();
    }

    const uint8_t *seq_img[SEQ_COUNT];
    for (unsigned i = 0; i < SEQ_COUNT; i++)
        seq_img[i] = (const uint8_t *)(SEQ_BASE + i * FLASH_PAGE_SIZE);
    const uint8_t *page = (const uint8_t *)SETTINGS_PAGE;
    app_init(&app, &io, page, page + CAL_OFFSET, seq_img);
    app_pedal_boot(&app, analog_read(ANALOG_PEDAL));
    app_switches(&app, (uint8_t)panel_sync_source(), panel_arp_mode());
    /* buttons held at boot (factory reset) must not act; start from the current state */
    app.buttons = b;
    btn_stable = b;
    led_level_init(&led_lvl, panel_led_full(), led_setting);

    uint32_t last_ms = millis(), touch_ms = last_ms;
    for (;;) {
        uint8_t byte;
        while (midi_uart_read(&byte))
            midi_parser_feed(&din_parser, byte, &din_cb);

        uint8_t pkt[4];
        while (usb_midi_receive(pkt)) {
            uint8_t n = usb_midi_packet_len(pkt);
            for (uint8_t i = 0; i < n; i++)
                midi_parser_feed(&usb_parser, pkt[1 + i], &usb_cb);
        }

        app_sync_in_run(&app, sync_in_run());
        for (unsigned n = sync_in_take_pulses(); n; n--)
            app_sync_in_pulse(&app);

        app_poll(&app, micros());

        /* Keybed on every pass: velocity is the time between a key's two contacts, and hard
         * strokes take only 1.5-4 ms, so a 1 ms scan is too coarse. */
        uint8_t rows[8];
        panel_scan_keys(rows);
        keyscan_update(&keys, rows, micros(), on_key, 0);

        uint32_t now = millis();
        if (now == last_ms)
            continue;
        last_ms = now;

        /* 1 ms tasks */
        led_brightness_tick();
        app_dc_power(&app, panel_led_full());
        keys.curve = settings_get(&app.set, P_VEL_CURVE);
        app_buttons(&app, debounce_buttons(panel_scan_buttons()));
        app_switches(&app, (uint8_t)panel_sync_source(), panel_arp_mode());
        app_knobs(&app, analog_read(ANALOG_TEMPO), analog_read(ANALOG_MODE), analog_read(ANALOG_TIMEDIV));
        app_pedal(&app, analog_read(ANALOG_PEDAL));
        app_aftertouch(&app, analog_read(ANALOG_AFTERTOUCH));

        /* Touch strips: an I2C read blocks for about 0.4 ms, so read one strip every 2 ms
         * (each at 250 Hz) to keep the pauses in the key scan short. */
        if (now - touch_ms >= 2u) {
            static uint8_t which;
            touch_ms = now;
            app_touch(&app, which, touch_read(which ? TOUCH_MOD : TOUCH_PITCH));
            which ^= 1u;
        }

        save_tick();
        clock_sent = 0;
    }
}
