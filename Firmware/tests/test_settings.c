#include "test.h"
#include "../src/core/settings.h"

int main(void)
{
    settings_t s;
    uint8_t img[SETTINGS_SIZE];
    for (unsigned i = 0; i < SETTINGS_SIZE; i++)
        img[i] = 0xFF;
    settings_load(&s, img);
    CHECK_EQ(settings_get(&s, P_MOD_CC), 1);
    CHECK_EQ(settings_get(&s, P_TRANSPOSE_CENTER), 60);
    CHECK_EQ(settings_get(&s, P_LED_BRIGHTNESS), 100);
    CHECK_EQ(settings_channel(&s, P_BEND_CH), 0);

    CHECK_EQ(settings_set(&s, P_USER_CH, 20), 15);
    CHECK_EQ(settings_channel(&s, P_BEND_CH), 15);
    CHECK_EQ(settings_set(&s, P_BEND_CH, 3), 3);
    CHECK_EQ(settings_channel(&s, P_BEND_CH), 3);
    CHECK_EQ(settings_set(&s, P_BEND_CH, 0x50), 15);
    CHECK_EQ(settings_set(&s, P_TAP_AVG, 0), 2);
    CHECK_EQ(settings_set(&s, P_MIDI_THRU, 9), 1);
    CHECK(s.dirty);

    for (unsigned i = 0; i < SETTINGS_SIZE; i++)
        img[i] = 0;
    img[P_TIE_CONV] = 7;
    img[P_LED_BRIGHTNESS] = 3;
    img[P_TRANSPOSE_CH] = 40;
    img[P_TEST_MODE] = 1;
    settings_load(&s, img);
    CHECK_EQ(settings_get(&s, P_TIE_CONV), 0);
    CHECK_EQ(settings_get(&s, P_LED_BRIGHTNESS), 100);
    CHECK_EQ(settings_get(&s, P_TRANSPOSE_CH), 15);
    CHECK_EQ(settings_get(&s, P_TEST_MODE), 0);
    return test_report("settings");
}
