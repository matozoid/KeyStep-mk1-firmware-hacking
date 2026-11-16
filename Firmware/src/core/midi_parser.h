/* Byte-stream MIDI parser (DIN input): running status, real-time bytes interleaved anywhere,
 * SysEx delivered in chunks. */
#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stdint.h>

#define MIDI_SYSEX_CHUNK 48

typedef struct {
    void *ctx;
    /* Channel and system common messages; len = 1..3 including the status byte. */
    void (*message)(void *ctx, const uint8_t *msg, uint8_t len);
    /* Real-time bytes (0xF8..0xFF). */
    void (*realtime)(void *ctx, uint8_t status);
    /* SysEx data, including the F0/F7 framing; last = 1 on the final chunk. */
    void (*sysex)(void *ctx, const uint8_t *data, uint8_t len, int last);
} midi_parser_cb_t;

typedef struct {
    uint8_t status;   /* running status, 0 = none */
    uint8_t expected; /* data bytes per message for status */
    uint8_t n;
    uint8_t msg[3];
    uint8_t in_sysex;
    uint8_t sx_len;
    uint8_t sx[MIDI_SYSEX_CHUNK];
} midi_parser_t;

void midi_parser_init(midi_parser_t *p);
void midi_parser_feed(midi_parser_t *p, uint8_t byte, const midi_parser_cb_t *cb);

/* Number of data bytes that follow a status byte (0..2), or -1 for SysEx/undefined. */
int midi_data_len(uint8_t status);

#endif
