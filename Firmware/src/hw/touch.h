/* Touch strip controllers: one per I2C bus, 7-bit address 0x15 (Docs/re/hardware_map.md). */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

enum touch_strip_id { TOUCH_PITCH = 0, TOUCH_MOD = 1 }; /* I2C1 PB6/PB7, I2C2 PB10/PB11 */

/* Returns a bit mask of strips that initialised. */
unsigned touch_init(void);

/* Read the raw position (register 0x21) of one strip; 0 = not touched. Returns -1 on bus error
 * (the bus is recovered and the chip re-initialised on the next call). */
int touch_read(enum touch_strip_id id);

#endif
