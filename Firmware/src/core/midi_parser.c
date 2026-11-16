#include "midi_parser.h"

int midi_data_len(uint8_t status)
{
    if (status < 0x80)
        return -1;
    if (status < 0xF0) {
        uint8_t hi = status & 0xF0u;
        return (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
    }
    switch (status) {
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    case 0xF6: return 0;
    default: return status >= 0xF8 ? 0 : -1;
    }
}

void midi_parser_init(midi_parser_t *p)
{
    p->status = 0;
    p->expected = 0;
    p->n = 0;
    p->in_sysex = 0;
    p->sx_len = 0;
}

static void sysex_byte(midi_parser_t *p, uint8_t b, const midi_parser_cb_t *cb, int last)
{
    p->sx[p->sx_len++] = b;
    if (last || p->sx_len == MIDI_SYSEX_CHUNK) {
        if (cb->sysex)
            cb->sysex(cb->ctx, p->sx, p->sx_len, last);
        p->sx_len = 0;
    }
}

static void end_sysex(midi_parser_t *p, const midi_parser_cb_t *cb)
{
    if (p->in_sysex) {
        p->in_sysex = 0;
        sysex_byte(p, 0xF7, cb, 1);
    }
}

void midi_parser_feed(midi_parser_t *p, uint8_t b, const midi_parser_cb_t *cb)
{
    if (b >= 0xF8) { /* real-time: never disturbs anything */
        if (cb->realtime)
            cb->realtime(cb->ctx, b);
        return;
    }

    if (b & 0x80) {
        if (b == 0xF0) {
            end_sysex(p, cb); /* unterminated previous SysEx */
            p->in_sysex = 1;
            p->sx_len = 0;
            p->status = 0;
            sysex_byte(p, b, cb, 0);
            return;
        }
        if (b == 0xF7) {
            end_sysex(p, cb);
            return;
        }
        end_sysex(p, cb); /* any status byte terminates SysEx */

        int len = midi_data_len(b);
        if (len < 0) { /* undefined F4/F5: drop and clear running status */
            p->status = 0;
            return;
        }
        if (len == 0) { /* F6 tune request */
            p->status = 0;
            if (cb->message)
                cb->message(cb->ctx, &b, 1);
            return;
        }
        p->status = b;
        p->expected = (uint8_t)len;
        p->msg[0] = b;
        p->n = 0;
        return;
    }

    /* data byte */
    if (p->in_sysex) {
        sysex_byte(p, b, cb, 0);
        return;
    }
    if (!p->status)
        return; /* stray data */
    p->msg[1 + p->n++] = b;
    if (p->n == p->expected) {
        if (cb->message)
            cb->message(cb->ctx, p->msg, (uint8_t)(1 + p->n));
        p->n = 0;
        if (p->status >= 0xF0) /* system common has no running status */
            p->status = 0;
    }
}
