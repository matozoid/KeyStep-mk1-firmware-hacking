/* USB-MIDI 1.0 event packets (4 bytes: cable/CIN, then up to 3 MIDI bytes). */
#ifndef USB_MIDI_PACKET_H
#define USB_MIDI_PACKET_H

#include <stdint.h>

/* Encode one complete non-SysEx message (status + data, len 1..3). Returns 0 on bad input. */
int usb_midi_encode(uint8_t cable, const uint8_t *msg, uint8_t len, uint8_t out[4]);

/* SysEx stream encoder: feed bytes in order (F0 ... F7); emits a packet whenever one is full or the
 * SysEx ends. */
typedef struct {
    uint8_t n;
    uint8_t buf[3];
} usb_midi_sysex_enc_t;

typedef void (*usb_midi_packet_fn)(void *ctx, const uint8_t pkt[4]);
void usb_midi_sysex_feed(usb_midi_sysex_enc_t *e, uint8_t cable, uint8_t byte, usb_midi_packet_fn fn, void *ctx);

/* Number of meaningful MIDI bytes in a received packet (0 for reserved CINs). */
uint8_t usb_midi_packet_len(const uint8_t pkt[4]);

#endif
