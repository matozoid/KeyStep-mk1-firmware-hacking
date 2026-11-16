#ifndef PANEL_H
#define PANEL_H

#include <stdint.h>

/* Button bit numbers as returned by panel_scan_buttons(). */
enum button {
    BTN_HOLD = 0,
    BTN_SHIFT = 1,
    BTN_OCT_DOWN = 2,
    BTN_OCT_UP = 3,
    BTN_TAP = 4,
    BTN_REC = 5,
    BTN_STOP = 6,
    BTN_PLAY = 7,
};

enum sync_source { SYNC_INTERNAL = 0, SYNC_USB = 1, SYNC_MIDI = 2, SYNC_IN = 3 };

void panel_init(void);
void panel_scan_keys(uint8_t rows[8]);
uint8_t panel_scan_buttons(void); /* bit = enum button, 1 = pressed */
enum sync_source panel_sync_source(void);
int panel_arp_mode(void);
/* PD4: the stock firmware runs the LEDs at 90 % of the brightness setting while it reads high
 * and 20 % while low. What drives it is not known (power source detect?). */
int panel_led_full(void);

#endif
