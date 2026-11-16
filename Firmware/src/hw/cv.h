#ifndef CV_H
#define CV_H

#include <stdint.h>

enum gate_type { GATE_STRIG = 0, GATE_VTRIG_5V = 1, GATE_VTRIG_12V = 2 };

void cv_init(void);
void cv_pitch(uint16_t code);
void cv_mod(uint16_t value12);
void cv_set_gate_type(enum gate_type type);
void cv_gate(int on);
void cv_led_brightness(uint8_t percent);

#endif
