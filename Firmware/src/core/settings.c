#include "settings.h"

static const uint8_t defaults[][2] = {
    {P_MIDI_IN_CH, SETTINGS_CH_USER}, {P_PEDAL_MODE, 1}, {P_SUSTAIN_CH, SETTINGS_CH_USER},
    {P_SUSTAIN_HIGH, 127}, {P_SUSTAIN_CC, 64}, {P_HOLD_BTN_FUNCTION, 1}, {P_PEDAL_FUNCTION, 2},
    {P_MOD_CH, SETTINGS_CH_USER}, {P_MOD_CC, 1}, {P_BEND_CH, SETTINGS_CH_USER},
    {P_KBD_PLAY_CH, SETTINGS_CH_USER}, {P_KBD_FLAG, 2}, {P_SYNC_TYPE, 1}, {P_TRANSPOSE_CH, 15},
    {P_TAP_AVG, 3}, {P_SEQ_RESTART, 1}, {P_TRANSPOSE_LATCH, 1}, {P_TRANSPOSE_PORT, 1},
    {P_FIXED_VEL, 100}, {P_TRANSPOSE_CENTER, 60}, {P_GATE_TYPE, 2}, {P_HZV_BASE, 24},
    {P_NOTE_PRIORITY, 2}, {P_MOD_CV_MAX, 5}, {P_BEND_RANGE, 2}, {P_TRANSPORT_MODE, 1},
    {P_STOP_CH, SETTINGS_CH_USER}, {P_REC_CH, SETTINGS_CH_USER}, {P_PLAY_CH, SETTINGS_CH_USER},
    {P_STOP_CC, 51}, {P_REC_CC, 50}, {P_PLAY_CC, 54}, {P_LED_BRIGHTNESS, 100},
};

void settings_defaults(settings_t *s)
{
    for (unsigned i = 0; i < SETTINGS_SIZE; i++)
        s->v[i] = 0;
    for (unsigned i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        s->v[defaults[i][0]] = defaults[i][1];
    s->dirty = 0;
}

static void sanitize(settings_t *s)
{
    static const uint8_t bool_fields[] = {P_SEQ_OPT_2C, P_WAIT_BAR, P_TIE_CONV, P_APPEND_OVERWRITE};
    for (unsigned i = 0; i < sizeof bool_fields; i++)
        if (s->v[bool_fields[i]] > 1)
            s->v[bool_fields[i]] = 0;
    if (s->v[P_ARP_RANDOM] > 2)
        s->v[P_ARP_RANDOM] = 0;
    if (s->v[P_LED_BRIGHTNESS] < 10 || s->v[P_LED_BRIGHTNESS] > 100)
        s->v[P_LED_BRIGHTNESS] = 100;
    if (s->v[P_TRANSPOSE_CH] > 16)
        s->v[P_TRANSPOSE_CH] = 15;
    s->v[P_TEST_MODE] = 0; /* never boot into the factory test mode */
}

void settings_load(settings_t *s, const uint8_t image[SETTINGS_SIZE])
{
    settings_defaults(s);
    if (image[0] == 0xFF && image[1] == 0xFF && image[2] == 0xFF && image[3] == 0xFF)
        return;
    for (unsigned i = 0; i < SETTINGS_SIZE; i++)
        s->v[i] = image[i];
    sanitize(s);
}

static uint8_t clampu(uint8_t v, uint8_t lo, uint8_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

uint8_t settings_set(settings_t *s, uint8_t p, uint8_t v)
{
    p &= 0x7F;
    switch (p) {
    case P_MIDI_IN_CH: case P_SUSTAIN_CH: case P_MOD_CH: case P_BEND_CH: case P_KBD_PLAY_CH:
    case P_STOP_CH: case P_REC_CH: case P_PLAY_CH:
        if (v != SETTINGS_CH_USER && v > 15)
            v = 15;
        break;
    case P_USER_CH: v = clampu(v, 0, 15); break;
    case P_MIDI_THRU: case P_PEDAL_MODE: case P_HOLD_BTN_MODE: case P_SYNC_START: case P_SEQ_RESTART:
    case P_TRANSPOSE_LATCH: case P_TEMPO_PICKUP: case P_FIXED_VEL_ON: case P_CV_HZV:
    case 0x67: case 0x68: case 0x69: case 0x6A:
        v = v != 0;
        break;
    case P_HOLD_BTN_FUNCTION: case P_PEDAL_FUNCTION: case P_TRANSPOSE_PORT: v = clampu(v, 1, 3); break;
    case P_KBD_FLAG: case P_NEXT_SEQ: case P_GATE_TYPE: case P_NOTE_PRIORITY: case P_MOD_CV_SOURCE:
        v = clampu(v, 0, 2);
        break;
    case P_AT_CURVE: case P_SYNC_TYPE: v = clampu(v, 0, 4); break;
    case P_VEL_CURVE: v = clampu(v, 0, 5); break;
    case P_TRANSPOSE_CH: v = clampu(v, 0, 16); break;
    case P_TAP_AVG: v = clampu(v, 2, 4); break;
    case P_FIXED_VEL: v = clampu(v, 1, 255); break;
    case P_MOD_CV_MAX: v = clampu(v, 1, 12); break;
    case P_BEND_RANGE: v = clampu(v, 1, 24); break;
    case P_TRANSPORT_MODE: v = clampu(v, 0, 3); break;
    case P_LED_BRIGHTNESS: v = clampu(v, 10, 100); break;
    default: break;
    }
    s->v[p] = v;
    s->dirty = 1;
    return v;
}

uint8_t settings_channel(const settings_t *s, uint8_t p)
{
    uint8_t c = s->v[p & 0x7F];
    return c == SETTINGS_CH_USER ? (uint8_t)(s->v[P_USER_CH] & 15) : (uint8_t)(c & 15);
}
