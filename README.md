# KeyStep Mk1 firmware hacking

Replacement firmware for the **Arturia KeyStep Mk1**: a better MIDI/CV controller, written in C.

This repo holds the groundwork: the reverse-engineered hardware map of the stock firmware, the update
protocol, and tools to unpack, analyse and upload firmware. The new firmware itself has not been
written yet.

> Forked from **KeyFreak** by Eduardo Mansilla (Mapache Bastardx), which turned the KeyStep into a
> small standalone synth. The synth part is not continued here. The bootloader protocol, the keyboard
> matrix and the upload tools come from that project.

## Status

| Area | State |
|---|---|
| Bootloader / SysEx update protocol | Known, upload tool works |
| `.led` update format | Known, pack and unpack tools |
| Hardware map (pins, peripherals, flash layout) | Mostly done — see [`Docs/re/hardware_map.md`](Docs/re/hardware_map.md) |
| Stock firmware symbols | 118 functions named — [`Docs/re/symbols.tsv`](Docs/re/symbols.tsv) |
| Pitch CV calibration format, settings layout, USB descriptors | Open |
| Sequencer / arpeggiator behaviour | Open |
| New firmware | Not started |

## Hardware at a glance

STM32F103VC (Cortex-M3, 72 MHz from 8 MHz HSE, 256 KB flash, 48 KB RAM). Arturia bootloader at
`0x08000000–0x08003FFF`; the application is linked at `0x08004000`.

| Part | Hardware |
|---|---|
| Keybed, 32 keys × 2 contacts (velocity) | Select PE8–PE15, read PD8–PD15, active low |
| 8 buttons | Row select PE7 / PB2, read PD8–PD11 |
| Sync-source switch, Seq/Arp switch | PE2/PE3, PE4 |
| Tempo, sustain pedal, mode, time division, aftertouch | ADC1 on PC0–PC4 |
| Pitch-bend and mod touch strips | Capacitive controllers at I2C address 0x2A, on I2C1 (PB6/PB7) and I2C2 (PB10/PB11) |
| Pitch CV | Two 8-bit PWM outputs on TIM3: PC6 (high byte) + PC7 (low byte) |
| Mod CV | DAC channel 1, PA4 (KeyFreak reports PA5 — verify on hardware) |
| Gate | PA1 (inverted), level select PA2/PA0 |
| Sync in / out | In: clock PD0, start/stop PD3. Out: clock PB8, start/stop PD2 |
| 8 LEDs | Shift register on SPI2 (PB13/PB15), latch PB12, brightness PWM PC9 |
| MIDI | DIN on USART1 (PA9/PA10), USB FS MIDI with D+ pull-up on PA8 |

Full details, evidence addresses and open questions are in the
[hardware map](Docs/re/hardware_map.md).

## Updating the firmware

Hold **Record + Stop + Play** while plugging in USB. The KeyStep shows up as
`Arturia KeyStep Updater` and accepts firmware as MIDI SysEx. The protocol is documented in
[`Docs/bootloader_protocol.md`](Docs/bootloader_protocol.md). The bootloader stays intact, so you can
always go back to the official firmware with Arturia's updater.

## Repository layout

```text
Docs/
  bootloader_protocol.md   SysEx update protocol
  hardware_notes.md        board notes from KeyFreak
  keyboard_matrix.md       keybed wiring
  re/hardware_map.md       full pin/peripheral map from the stock firmware
  re/symbols.tsv           address → name table for Ghidra
Tools/
  Ghidra/                  headless Ghidra scripts (import, decompile, xrefs, apply symbols)
  Ledpacker/               .led ↔ .bin conversion
  MidiProbe/               bootloader probing and .led inspection
  Uploader/                raw-MIDI firmware uploader
  UsbProtocol/             USB capture analysis
```

## Reverse engineering setup

1. Download the official KeyStep firmware update from Arturia yourself.
2. Put it in `vendor/` (gitignored) and unpack it:
   `python3 Tools/Ledpacker/led_to_bin.py vendor/<update>.led vendor/keystep_1_0_28.bin`
3. Import and analyse it with Ghidra as described in [`Tools/Ghidra/README.md`](Tools/Ghidra/README.md),
   then apply `Docs/re/symbols.tsv`.

## Rules for this repo

- No official `.led` files, binary dumps or other Arturia material is committed. Bring your own copy
  and keep it in `vendor/`.
- Documentation describes the hardware and behaviour in our own words. No decompiled code is
  copied in.
- Not affiliated with Arturia. Trademarks are used for descriptive purposes only.

## Safety

Flashing custom firmware can leave the unit unusable, at your own risk. The CV outputs are not audio
outputs: don't plug headphones into them. Measure before connecting anything to internal points.
