#ifndef MIDI_UART_H
#define MIDI_UART_H

#include <stdint.h>

void midi_uart_init(void);
int midi_uart_send(const uint8_t *data, unsigned len); /* all-or-nothing; 0 if the queue is full */
int midi_uart_read(uint8_t *b);

#endif
