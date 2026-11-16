/* Note + pitch bend -> 16-bit pitch CV code (hardware independent).
 * See Docs/re/pitch_cv.md for the output scale: 533 codes per semitone, 6396 per volt. */
#ifndef PITCH_CV_H
#define PITCH_CV_H

#include <stdint.h>

#define PITCH_CV_CODES_PER_SEMI 533
#define PITCH_CV_CODES_PER_VOLT (12 * PITCH_CV_CODES_PER_SEMI)
#define PITCH_CV_MAX_CODE       0xFFFF

enum pitch_cv_mode { PITCH_CV_VOCT = 0, PITCH_CV_HZV = 1 };

typedef struct {
    uint8_t mode;       /* enum pitch_cv_mode */
    uint8_t voct_base;  /* note that gives 0 V in V/oct mode (default 0) */
    uint8_t hzv_base;   /* note that gives 1 V in Hz/V mode (default 24) */
    uint8_t bend_range; /* semitones, 1..24 (default 2) */
} pitch_cv_config_t;

void pitch_cv_config_default(pitch_cv_config_t *cfg);

/* note: MIDI note 0..127; bend: 14-bit MIDI pitch bend, 0x2000 = centre. */
uint16_t pitch_cv_code(const pitch_cv_config_t *cfg, uint8_t note, uint16_t bend);

/* Hz/V helper: code for table index i (i = note - base + 48), 22-bit fixed point (code << 6). */
uint32_t pitch_cv_hzv_q6(int i);

#endif
