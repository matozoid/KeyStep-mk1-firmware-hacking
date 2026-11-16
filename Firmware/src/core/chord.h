/* Chord mode (Docs/re/arp_chord.md "Chord mode"): capture a chord, then every played note sounds
 * the chord transposed so that its root lands on the played note. */
#ifndef CHORD_H
#define CHORD_H

#include <stdint.h>

#define CHORD_MAX 16

enum chord_state { CHORD_OFF = 0, CHORD_ON = 1, CHORD_CAPTURING = 2 };

typedef struct {
    uint8_t notes[CHORD_MAX]; /* notes[0] is the root (lowest captured note) */
    uint8_t n;
    uint8_t state;
    uint8_t state_before;
    uint8_t captured;         /* a note was captured in this session */
} chord_t;

void chord_init(chord_t *c);                      /* default chord: {60} */
void chord_capture_begin(chord_t *c);             /* Shift+Hold pressed */
void chord_capture_end(chord_t *c);               /* Shift+Hold released */
void chord_capture_note(chord_t *c, uint8_t note); /* while capturing */

/* Expand a played note into the chord; returns the number of notes written to out. */
unsigned chord_expand(const chord_t *c, uint8_t played, uint8_t out[CHORD_MAX]);

#endif
