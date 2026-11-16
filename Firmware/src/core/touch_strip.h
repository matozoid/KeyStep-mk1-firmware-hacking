/* Pitch-bend and mod strip logic (Docs/re/keyboard.md sections 9 and 10). Hardware independent:
 * feed raw position bytes from the touch controller (0 = not touched). */
#ifndef TOUCH_STRIP_H
#define TOUCH_STRIP_H

#include <stdint.h>

#define TOUCH_NONE 0x80 /* scaled value when the strip is not touched */

/* Calibration as stored at flash 0x0803F200: per strip {hi, lo}; erased flash = {0xBF, 0x09}. */
typedef struct {
    uint8_t lo, hi;
} touch_cal_t;

/* raw -> 0..127, or TOUCH_NONE for raw 0 */
uint8_t touch_scale(uint8_t raw, touch_cal_t cal);

/* scaled 0..127 -> bend value 0..127 with the centre dead zone */
uint8_t touch_pitch_map(uint8_t s);

typedef struct {
    uint8_t last;     /* last value sent, 0xFF = none */
    uint8_t active;   /* bend away from centre: spring back on release */
    uint8_t spring_idx;
    uint8_t spring_up;
} pitch_strip_t;

void pitch_strip_init(pitch_strip_t *p);

/* One new sample. Returns the bend value 0..127 to send, or -1 for nothing. The 14-bit pitch bend
 * is v << 7, with LSB 0x7F when v == 127. While springing back each new sample steps toward 64. */
int pitch_strip_update(pitch_strip_t *p, uint8_t raw, touch_cal_t cal);

typedef struct {
    uint8_t last; /* 0xFF = none */
} mod_strip_t;

void mod_strip_init(mod_strip_t *m);
/* Returns the CC value 0..127 to send (only while touched and changed), or -1. */
int mod_strip_update(mod_strip_t *m, uint8_t raw, touch_cal_t cal);

#endif
