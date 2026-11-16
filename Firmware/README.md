# KeyStep Mk1 open firmware

Replacement application firmware for the Arturia KeyStep Mk1 (STM32F103VC), written from scratch
in C. It is linked at `0x08004000` and installed with the stock bootloader, which stays untouched,
so the official firmware can always be reinstalled.

Status: **runs on hardware, partly verified**. Every stock feature is implemented in
`src/core/app.c` and exercised by host tests. On a real unit these have been checked: boot, USB MIDI,
keys and velocity, aftertouch, touch strips, buttons, knobs, switches, LEDs, arp, sequencer play and
step record, sequences saved by the stock firmware, saving across a power cycle, and both ways into
the updater. Not checked yet: CV/gate, sync, DIN MIDI, pedal, MCC. See
[HARDWARE_TEST.md](HARDWARE_TEST.md).

| Area | State |
|---|---|
| Clock, SysTick, startup, linker script | done |
| Keybed scan + velocity | done (own curves; same key matrix indexing as stock) |
| Buttons, switches, knobs, LEDs | done, including the brightness fades and the power-up light show |
| Pitch CV (V/oct, Hz/V, bend), gate types, mod CV | done (mod CV full scale to measure) |
| DIN + USB MIDI, thru, MIDI-to-CV, RPN bend range | done (pid.codes test VID/PID by default; `make STOCK_USB_ID=1` for the stock IDs) |
| Touch strips (I2C), aftertouch, pedal / Hold button | done |
| Sync in/out (all sync types), MIDI clock master/slave, tap tempo | done |
| 8 sequences: step + real-time record, ties, rests, swing, gate, transpose, Kbd Play | done |
| Arpeggiator (8 modes, range, hold), chord capture | done |
| Transport CC / MMC, triple-Stop panic | done |
| Settings in flash, MCC SysEx: settings, sequence transfer, strip calibration, note preview, identity, updater | done |
| MCC factory-test and diagnostic SysEx | done (test mode, loopback tests, direct CV drive) |

### Deliberate differences from the stock firmware

- Flash saves block for a few ms, so they wait until the transport is stopped (stock saves in the
  background while playing). Sequences save on Stop or 10 s after an edit, settings 20 s after a
  SysEx change.
- Factory reset (Oct- + Oct+ at power-up) leaves the sequences empty; the stock demo sequences are
  not reproduced.
- Velocity and aftertouch curves are our own, with the same overall shapes.
- The USB identity reply reuses the stock version bytes so MIDI Control Center accepts the unit.

## Layout

```text
src/core/   hardware-independent logic, unit-tested on the host
src/hw/     STM32F103 drivers (own register definitions, no vendor HAL)
src/main.c  wiring
ld/         linker script (app at 0x08004000, data pages at 0x0803B000+)
tests/      host tests for src/core
tools/      generators (Hz/V table)
```

## Build

Needs `arm-none-eabi-gcc` and, for the tests, a host `gcc`.

```bash
make            # build/keystep.elf, build/keystep.bin
make test       # host unit tests (ASan/UBSan)
make led TEMPLATE=../vendor/keystep_1_0_28.led   # pack into an updater file (local only)
```

Flash with the test rig: `../Tools/TestRig/keystep_testrig.sh --flash build/keystep.bin`. It packs
the image, puts the unit in update mode over SysEx and uploads with `Tools/Uploader`. If no firmware
is running, hold Rec + Stop + Play while plugging in USB. See
[Docs/bootloader_protocol.md](../Docs/bootloader_protocol.md).

## Hardware reference

[Docs/re/hardware_map.md](../Docs/re/hardware_map.md) has the pin map; behaviour targets are in
[Docs/re/stock_features.md](../Docs/re/stock_features.md) and the other `Docs/re/*.md` files.
