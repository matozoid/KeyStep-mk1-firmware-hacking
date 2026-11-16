#include <string.h>
#include "test.h"
#include "../src/core/midi_parser.h"

static uint8_t log_buf[256];
static int log_len;

static void put(uint8_t b) { log_buf[log_len++] = b; }
static void on_msg(void *c, const uint8_t *m, uint8_t n)
{
    (void)c;
    put('M');
    for (int i = 0; i < n; i++)
        put(m[i]);
}
static void on_rt(void *c, uint8_t s)
{
    (void)c;
    put('R');
    put(s);
}
static void on_sx(void *c, const uint8_t *d, uint8_t n, int last)
{
    (void)c;
    put(last ? 'X' : 'x');
    for (int i = 0; i < n; i++)
        put(d[i]);
}

static const midi_parser_cb_t cb = {0, on_msg, on_rt, on_sx};

static void feed(midi_parser_t *p, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++)
        midi_parser_feed(p, b[i], &cb);
}

int main(void)
{
    midi_parser_t p;
    midi_parser_init(&p);

    /* running status with a clock byte in the middle of a message */
    static const uint8_t in1[] = {0x90, 60, 0xF8, 100, 62, 0, 0xC1, 5};
    /* the real-time byte is delivered before the message it interrupted */
    static const uint8_t out1b[] = {'R', 0xF8, 'M', 0x90, 60, 100, 'M', 0x90, 62, 0, 'M', 0xC1, 5};
    log_len = 0;
    feed(&p, in1, sizeof in1);
    CHECK_EQ(log_len, (int)sizeof out1b);
    CHECK(memcmp(log_buf, out1b, sizeof out1b) == 0);

    /* SysEx, then running status must be cleared */
    static const uint8_t in2[] = {0xF0, 0x00, 0x20, 0x6B, 0xF7, 0x40};
    static const uint8_t out2[] = {'X', 0xF0, 0x00, 0x20, 0x6B, 0xF7};
    log_len = 0;
    feed(&p, in2, sizeof in2);
    CHECK_EQ(log_len, (int)sizeof out2);
    CHECK(memcmp(log_buf, out2, sizeof out2) == 0);

    /* long SysEx is chunked */
    log_len = 0;
    midi_parser_feed(&p, 0xF0, &cb);
    for (int i = 0; i < 100; i++)
        midi_parser_feed(&p, (uint8_t)i, &cb);
    midi_parser_feed(&p, 0xF7, &cb);
    CHECK_EQ(log_buf[0], 'x');
    CHECK_EQ(log_buf[1 + MIDI_SYSEX_CHUNK], 'x');
    CHECK_EQ(log_buf[2 + 2 * MIDI_SYSEX_CHUNK], 'X');
    CHECK_EQ(log_len, 3 + 102);

    return test_report("midi_parser");
}
