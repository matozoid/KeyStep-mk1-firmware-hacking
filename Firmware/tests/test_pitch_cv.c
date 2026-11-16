#include "test.h"
#include "../src/core/pitch_cv.h"

static void test_voct(void)
{
    pitch_cv_config_t cfg;
    pitch_cv_config_default(&cfg);

    CHECK_EQ(pitch_cv_code(&cfg, 0, 0x2000), 0);
    CHECK_EQ(pitch_cv_code(&cfg, 12, 0x2000), 6396);       /* 1 V */
    CHECK_EQ(pitch_cv_code(&cfg, 60, 0x2000), 5 * 6396);
    CHECK_EQ(pitch_cv_code(&cfg, 127, 0x2000), 0xFFFF);    /* clamped instead of wrapping */

    cfg.voct_base = 36;
    CHECK_EQ(pitch_cv_code(&cfg, 30, 0x2000), 0);          /* below the 0 V note */
    CHECK_EQ(pitch_cv_code(&cfg, 48, 0x2000), 6396);

    /* full bend up/down with range 2 = +/- 2 semitones */
    cfg.voct_base = 0;
    CHECK_EQ(pitch_cv_code(&cfg, 60, 0x3FFF), 60 * 533 + ((8191 * 2 * 533) >> 13));
    CHECK_EQ(pitch_cv_code(&cfg, 60, 0x0000), 58 * 533);
    CHECK_EQ(pitch_cv_code(&cfg, 0, 0x0000), 0);           /* no negative output */
}

static void test_hzv(void)
{
    pitch_cv_config_t cfg;
    pitch_cv_config_default(&cfg);
    cfg.mode = PITCH_CV_HZV;

    CHECK_EQ(pitch_cv_code(&cfg, 24, 0x2000), 6396);       /* base note = 1 V */
    CHECK_EQ(pitch_cv_code(&cfg, 36, 0x2000), 2 * 6396);   /* octave up doubles */
    CHECK_EQ(pitch_cv_code(&cfg, 12, 0x2000), 6396 / 2);
    CHECK_EQ(pitch_cv_code(&cfg, 127, 0x2000), pitch_cv_hzv_q6(88) >> 6); /* clamps at top entry */

    /* bend of exactly one semitone lands on the next note */
    cfg.bend_range = 1;
    uint16_t up = pitch_cv_code(&cfg, 24, 0x2000 + 8191);
    uint16_t next = pitch_cv_code(&cfg, 25, 0x2000);
    CHECK(up <= next && next - up < 4);
    CHECK_EQ(pitch_cv_code(&cfg, 24, 0x0000), pitch_cv_code(&cfg, 23, 0x2000));
}

int main(void)
{
    test_voct();
    test_hzv();
    return test_report("pitch_cv");
}
