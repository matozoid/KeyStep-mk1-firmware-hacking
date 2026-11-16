#ifndef ANALOG_H
#define ANALOG_H

#include <stdint.h>

enum analog_input {
    ANALOG_TEMPO = 0,
    ANALOG_PEDAL = 1,
    ANALOG_MODE = 2,
    ANALOG_TIMEDIV = 3,
    ANALOG_AFTERTOUCH = 4,
    ANALOG_COUNT = 5,
};

void analog_init(void);
uint16_t analog_read(enum analog_input in); /* raw 12-bit */

#endif
