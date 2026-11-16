/* Arpeggiator (Docs/re/arp_chord.md). Like the stock firmware it is a pattern generator: it writes
 * the held notes into a sequence_t that the ordinary sequence player plays, so swing, gate, ties
 * and fixed velocity behave exactly as in Seq mode. */
#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include "sequence.h"
#include "seq_player.h"

#define ARP_MAX_NOTES 32

enum arp_mode {
    ARP_UP, ARP_DOWN, ARP_INCLUSIVE, ARP_EXCLUSIVE, ARP_RANDOM, ARP_ORDER, ARP_UP2, ARP_DOWN2,
};

typedef struct {
    uint8_t n;
    uint8_t note[ARP_MAX_NOTES], vel[ARP_MAX_NOTES]; /* insertion order */
} note_list_t;

typedef struct {
    sequence_t pattern;
    seq_player_t *player;

    note_list_t held;     /* notes in the arp (physically held or latched) */
    note_list_t released; /* latched notes whose key is up */
    uint8_t keys_down;

    uint32_t rng;
    uint8_t rng_seeded;
    uint8_t hold;
    uint8_t running;      /* transport playing */
    uint8_t mode;
    uint8_t random_variant; /* 0 per-step, 1 fixed riff, 2 random walk */
    uint8_t random_len;
    uint8_t walk_pos;
    int8_t octave;        /* current octave offset */
    int8_t range;         /* -3..+3 */
    uint8_t dirty;        /* rebuild at the next loop point */
} arp_t;

void note_list_clear(note_list_t *l);
int note_list_add(note_list_t *l, uint8_t note, uint8_t vel);
int note_list_remove(note_list_t *l, uint8_t note);
int note_list_has(const note_list_t *l, uint8_t note);

/* player may be 0 in tests; when set the arp installs its step hook and drives octave_shift. */
void arp_init(arp_t *a, seq_player_t *player);
void arp_note_on(arp_t *a, uint8_t note, uint8_t vel);
void arp_note_off(arp_t *a, uint8_t note);
void arp_set_hold(arp_t *a, int on);
void arp_set_mode(arp_t *a, uint8_t mode);
void arp_set_range(arp_t *a, int8_t range);
void arp_set_random_variant(arp_t *a, uint8_t variant);
void arp_transport(arp_t *a, int running); /* start resets the octave cycle; stop clears everything */
void arp_seed(arp_t *a, uint32_t seed);

/* Build the pattern from the held notes now (normally done at the loop point). */
void arp_build(arp_t *a);

/* seq_step_hook implementation (installed by arp_init when a player is given). */
uint8_t arp_step_hook(void *ctx, uint8_t step, int wrapped);

#endif
