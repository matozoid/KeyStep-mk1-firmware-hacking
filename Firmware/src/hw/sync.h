/* Sync In / Sync Out jacks (Docs/re/hardware_map.md):
 *   PD0 clock in (EXTI0), PD3 start/stop in (EXTI3), PB8 clock out, PD2 start/stop out. */
#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>

void sync_init(void);

/* Number of Sync In clock edges since the last call (counted in the EXTI0 interrupt). */
unsigned sync_in_take_pulses(void);

/* Level of the Sync In start/stop line, 1 = running. */
int sync_in_run(void);

void sync_out(int clock, int run);

/* Factory loopback test (sysex.md 5.2): with a cable from Sync Out to Sync In, drive the clock
 * (line 0) or start/stop (line 1) output low and high five times and check that the input
 * follows. Returns 1 on pass. Leaves both outputs idle. */
int sync_loop_test(int line);

#endif
