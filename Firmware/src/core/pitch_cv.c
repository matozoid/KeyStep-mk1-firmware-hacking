#include "pitch_cv.h"
#include "hzv_table.h"

void pitch_cv_config_default(pitch_cv_config_t *cfg)
{
    cfg->mode = PITCH_CV_VOCT;
    cfg->voct_base = 0;
    cfg->hzv_base = 24;
    cfg->bend_range = 2;
}

uint32_t pitch_cv_hzv_q6(int i)
{
    if (i < 0)
        return 0;
    if (i >= HZV_TABLE_SIZE)
        i = HZV_TABLE_SIZE - 1;
    return hzv_table[i] >> 10;
}

static uint16_t clamp_code(int32_t code)
{
    if (code < 0)
        return 0;
    if (code > PITCH_CV_MAX_CODE)
        return PITCH_CV_MAX_CODE;
    return (uint16_t)code;
}

uint16_t pitch_cv_code(const pitch_cv_config_t *cfg, uint8_t note, uint16_t bend)
{
    int up = bend >= 0x2000;
    int32_t d = (int32_t)(up ? bend - 0x2000 : 0x2000 - bend) * cfg->bend_range; /* 1/8192 semitone */

    if (cfg->mode == PITCH_CV_VOCT) {
        int32_t code = ((int32_t)note - cfg->voct_base) * PITCH_CV_CODES_PER_SEMI;
        if (code < 0)
            code = 0;
        int32_t delta = (d * PITCH_CV_CODES_PER_SEMI) >> 13;
        return clamp_code(up ? code + delta : code - delta);
    }

    /* Hz/V: interpolate between neighbouring semitones of the exponential table. */
    int i = (int)note - cfg->hzv_base + 48;
    int semis = (int)(d >> 13);
    int32_t frac = d & 0x1FFF;
    int step = up ? 1 : -1;
    int i0 = i + step * semis;
    int32_t h0 = (int32_t)pitch_cv_hzv_q6(i0);
    int32_t h1 = (int32_t)pitch_cv_hzv_q6(i0 + step);
    int32_t q6 = h0 + (int32_t)(((int64_t)frac * (h1 - h0)) >> 13);
    return clamp_code(q6 >> 6);
}
