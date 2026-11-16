#ifndef USB_MIDI_H
#define USB_MIDI_H

#include <stdint.h>

void usb_midi_init(void);
int usb_midi_ready(void);
int usb_midi_send(const uint8_t *msg, uint8_t len);  /* one non-SysEx message */
int usb_midi_send_packet(const uint8_t pkt[4]);       /* raw USB-MIDI event packet */
int usb_midi_receive(uint8_t pkt[4]);                 /* 1 if a packet was dequeued */

unsigned usb_midi_tx_room(void);                      /* free packets in the send queue */
unsigned usb_midi_tx_sent(void);                      /* counter that moves when the host reads */

#endif
