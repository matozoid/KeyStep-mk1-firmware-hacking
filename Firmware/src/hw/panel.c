/* Keybed matrix, buttons and switches (see Docs/keyboard_matrix.md and Docs/re/hardware_map.md).
 *
 * Keybed: select PE8-PE15 (one low at a time), read PD8-PD15 (active low).
 * Buttons: select PE7 (row 0) or PB2 (row 1) low, read PD8-PD11 (active low).
 * Switches: PE2/PE3 sync source (inverted 2-bit), PE4 Seq/Arp. */
#include "panel.h"
#include "system.h"

static void settle(void)
{
    for (volatile int i = 0; i < 40; i++) {
    }
}

void panel_init(void)
{
    for (unsigned p = 8; p < 16; p++) {
        gpio_config(GPIOE, p, GPIO_OUT_PP);
        gpio_config(GPIOD, p, GPIO_IN_PULL);
    }
    GPIOE->BSRR = 0xFF00u;  /* all keybed selects high (inactive) */
    GPIOD->BSRR = 0xFF00u;  /* pull-ups on the read lines */

    gpio_config(GPIOE, 7, GPIO_OUT_PP);
    gpio_config(GPIOB, 2, GPIO_OUT_PP);
    gpio_write(GPIOE, 7, 1);
    gpio_write(GPIOB, 2, 1);

    for (unsigned p = 2; p <= 4; p++) {
        gpio_config(GPIOE, p, GPIO_IN_PULL);
        gpio_write(GPIOE, p, 1);
    }

    gpio_config(GPIOD, 4, GPIO_IN_FLOATING); /* LED level select (hardware_map.md) */
}

int panel_led_full(void)
{
    return gpio_read(GPIOD, 4);
}

void panel_scan_keys(uint8_t rows[8])
{
    for (unsigned r = 0; r < 8; r++) {
        GPIOE->BSRR = (1u << (8 + r + 16)) | (0xFF00u & ~(1u << (8 + r)));
        settle();
        rows[r] = (uint8_t)~(GPIOD->IDR >> 8);
    }
    GPIOE->BSRR = 0xFF00u;
}

uint8_t panel_scan_buttons(void)
{
    uint8_t bits;

    gpio_write(GPIOE, 7, 0);
    settle();
    bits = (uint8_t)(~(GPIOD->IDR >> 8) & 0x0Fu);
    gpio_write(GPIOE, 7, 1);

    gpio_write(GPIOB, 2, 0);
    settle();
    bits |= (uint8_t)((~(GPIOD->IDR >> 8) & 0x0Fu) << 4);
    gpio_write(GPIOB, 2, 1);
    return bits;
}

enum sync_source panel_sync_source(void)
{
    /* Stock (hardware_map.md, midi.md 5): code = ~(PE3 | PE2 << 1) & 3, where code 0 Internal,
     * 1 USB, 2 Sync In, 3 MIDI. */
    static const enum sync_source map[4] = {SYNC_INTERNAL, SYNC_USB, SYNC_IN, SYNC_MIDI};
    unsigned idr = GPIOE->IDR;
    unsigned code = ~(((idr >> 3) & 1u) | (((idr >> 2) & 1u) << 1)) & 3u;
    return map[code];
}

int panel_arp_mode(void)
{
    return !gpio_read(GPIOE, 4); /* stock: PE4 high = Seq, low = Arp (ui_set_seq_arp) */
}
