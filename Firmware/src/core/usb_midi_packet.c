#include "usb_midi_packet.h"

int usb_midi_encode(uint8_t cable, const uint8_t *msg, uint8_t len, uint8_t out[4])
{
    uint8_t s = msg[0], cin;
    if (!(s & 0x80) || len < 1 || len > 3)
        return 0;
    if (s < 0xF0) {
        cin = s >> 4;
    } else if (s >= 0xF8 || s == 0xF6) {
        cin = s >= 0xF8 ? 0xF : 0x5;
    } else if (s == 0xF2) {
        cin = 0x3;
    } else if (s == 0xF1 || s == 0xF3) {
        cin = 0x2;
    } else {
        return 0; /* SysEx goes through usb_midi_sysex_feed */
    }
    out[0] = (uint8_t)((cable << 4) | cin);
    out[1] = s;
    out[2] = len > 1 ? msg[1] : 0;
    out[3] = len > 2 ? msg[2] : 0;
    return 1;
}

void usb_midi_sysex_feed(usb_midi_sysex_enc_t *e, uint8_t cable, uint8_t byte, usb_midi_packet_fn fn, void *ctx)
{
    if (byte == 0xF0)
        e->n = 0;
    e->buf[e->n++] = byte;
    if (byte == 0xF7 || e->n == 3) {
        uint8_t pkt[4] = {0, e->buf[0], e->n > 1 ? e->buf[1] : 0, e->n > 2 ? e->buf[2] : 0};
        uint8_t cin = byte == 0xF7 ? (uint8_t)(0x4 + e->n) : 0x4; /* 5/6/7 = end with 1/2/3 bytes */
        pkt[0] = (uint8_t)((cable << 4) | cin);
        fn(ctx, pkt);
        e->n = 0;
    }
}

uint8_t usb_midi_packet_len(const uint8_t pkt[4])
{
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    return len[pkt[0] & 0x0F];
}
