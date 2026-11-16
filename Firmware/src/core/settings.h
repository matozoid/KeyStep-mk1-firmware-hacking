/* Global settings in the stock layout (Docs/re/settings.md): 0x80 bytes indexed by the MCC/SysEx
 * parameter code p, stored at flash 0x0803F000. Keeping the layout means settings made with MCC
 * under the stock firmware carry over, and switching back keeps working. */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#define SETTINGS_SIZE 0x80
#define SETTINGS_CH_USER 0x41 /* channel value meaning "follow the user channel" */

enum settings_param {
    P_MIDI_IN_CH = 0x07,
    P_MIDI_THRU = 0x08,
    P_PEDAL_MODE = 0x09,
    P_SUSTAIN_CH = 0x0A,
    P_SUSTAIN_LOW = 0x0B,
    P_SUSTAIN_HIGH = 0x0C,
    P_SUSTAIN_CC = 0x0D,
    P_HOLD_BTN_MODE = 0x0E,
    P_HOLD_BTN_FUNCTION = 0x0F,
    P_PEDAL_FUNCTION = 0x10,
    P_MOD_CH = 0x13,
    P_MOD_CC = 0x14,
    P_BEND_CH = 0x15,
    P_USER_CH = 0x16,
    P_KBD_PLAY_CH = 0x17,
    P_KBD_FLAG = 0x18,
    P_VEL_CURVE = 0x19,
    P_AT_CURVE = 0x1A,
    P_SYNC_TYPE = 0x20,
    P_TRANSPOSE_CH = 0x21,
    P_TAP_AVG = 0x22,
    P_SYNC_START = 0x23,
    P_NEXT_SEQ = 0x24,
    P_SEQ_RESTART = 0x25,
    P_TRANSPOSE_LATCH = 0x26,
    P_TRANSPOSE_PORT = 0x27,
    P_TEMPO_PICKUP = 0x28,
    P_FIXED_VEL_ON = 0x29,
    P_FIXED_VEL = 0x2A,
    P_TRANSPOSE_CENTER = 0x2B,
    P_SEQ_OPT_2C = 0x2C,
    P_WAIT_BAR = 0x2D,
    P_TIE_CONV = 0x2E,
    P_ARP_RANDOM = 0x2F,
    P_APPEND_OVERWRITE = 0x30,
    P_CV_HZV = 0x46,
    P_GATE_TYPE = 0x47,
    P_VOCT_BASE = 0x4A,
    P_HZV_BASE = 0x4C,
    P_NOTE_PRIORITY = 0x4D,
    P_MOD_CV_SOURCE = 0x4E,
    P_MOD_CV_MAX = 0x4F,
    P_BEND_RANGE = 0x50,
    P_TRANSPORT_MODE = 0x60,
    P_STOP_CH = 0x61,
    P_REC_CH = 0x62,
    P_PLAY_CH = 0x63,
    P_STOP_CC = 0x64,
    P_REC_CC = 0x65,
    P_PLAY_CC = 0x66,
    P_PLAY_RESETS = 0x67,
    P_LED_BRIGHTNESS = 0x6B,
    P_TEST_MODE = 0x7D,
};

typedef struct {
    uint8_t v[SETTINGS_SIZE];
    uint8_t dirty;
} settings_t;

void settings_defaults(settings_t *s);
/* Load a stored image (erased first word = keep defaults), then repair invalid fields. */
void settings_load(settings_t *s, const uint8_t image[SETTINGS_SIZE]);
/* Clamp and store; returns the stored value. Marks dirty. */
uint8_t settings_set(settings_t *s, uint8_t p, uint8_t value);
static inline uint8_t settings_get(const settings_t *s, uint8_t p) { return s->v[p & 0x7F]; }
/* Channel parameter, resolving "User" (0x41) to the user channel. 0-based. */
uint8_t settings_channel(const settings_t *s, uint8_t p);

#endif
