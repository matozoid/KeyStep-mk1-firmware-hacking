# Hardware notes

Quick reference. The full map with evidence addresses is in
[`re/hardware_map.md`](re/hardware_map.md).

## Device

- Arturia KeyStep Mk1 (KeyStep 32)
- MCU: STM32F103VCT6, Cortex-M3, 72 MHz (8 MHz HSE × 9), 256 KB flash, 48 KB RAM
- Bootloader `0x08000000–0x08003FFF`, application at `0x08004000`

## Outputs

| Output | Stock firmware | Notes |
|---|---|---|
| Pitch CV | TIM3 PWM: PC6 (high byte) + PC7 (low byte), summed into one 16-bit voltage | Not a DAC output |
| Mod CV | DAC channel 1 (PA4), 12-bit | KeyFreak found audio on PA5 (DAC channel 2). Unresolved — measure on hardware |
| Gate | PA1, inverted; PA2/PA0 select the gate level | |

None of these are audio outputs. Don't connect headphones.

## Inputs

- Keybed: see [`keyboard_matrix.md`](keyboard_matrix.md).
- Buttons, switches, knobs, pedal, aftertouch, touch strips, sync and MIDI: see the hardware map.
