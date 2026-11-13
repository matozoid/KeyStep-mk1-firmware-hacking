# Pitch CV output

How the stock firmware (1.0.28) turns a note and pitch bend into the Pitch CV voltage.

**There is no per-unit calibration.** The note-to-voltage conversion is fixed arithmetic plus one
constant lookup table; nothing is read from flash or trimmed per device. Accuracy depends on the
analog output stage only.

## Output stage

A 16-bit code is split over two 8-bit PWM channels of TIM3 (full remap, period 0xFF):

| Pin | Channel | Gets |
|---|---|---|
| PC6 | TIM3_CH1 (CCR1) | `code >> 8` |
| PC7 | TIM3_CH2 (CCR2) | `code & 0xFF` |

The board presumably sums the two filtered PWMs with a 256:1 weighting. `pitch_cv_write8`
(0x08013114) only rewrites a CCR when the byte changes.

Scale: **533 codes per semitone, 6396 per octave = 1 V**. The full 16-bit range is ~10.25 V; the
manual specifies V/oct as 0–10 V (to be confirmed with a meter). The manual says Hz/V goes to
"~12 V", but the table tops out at ~10.08 V worth of code.

## Settings

The pitch CV object (`0x20000044`) copies these from the settings block
(`pitch_cv_on_settings` 0x08018f9c, event codes `'F'`, `'J'`, `'L'`, `'P'`):

| Object | Settings | Meaning | Default |
|---|---|---|---|
| +4 | +0x72 | CV mode: 0 = V/oct, 1 = Hz/V | 0 |
| +5 | +0x76 | V/oct: MIDI note that outputs 0 V | 0 (C-2) |
| +5 | +0x78 | Hz/V: MIDI note that outputs 1 V | 24 (C0) |
| +6 | +0x7C | Pitch-bend range, 1–24 semitones | 2 |

## Note → code (`pitch_cv_note_to_code` 0x08018eec)

**V/oct** (mode 0):

```c
code = (note < base) ? 0 : (note - base) * 533;
```

**Hz/V** (mode 1): exponential, voltage doubles per octave.

```c
i = note - base + 48;                     // 0..88
code = HZV(i) >> 16;                      // >> 10 when interpolating bend (see below)
HZV(i) = round(0x18FC0000 * 2^((i - 48) / 12))
```

So `note = base` gives 6396 codes = 1 V (manual: "1 V MIDI note", default C0 = MIDI 24); every 12
notes up doubles it, every 12 down halves it. Indices above 88 clamp to entry 88 (64467 codes ≈ 10.08 V). Below 0 the
output is 0 (?). The table is 89 × uint32 at 0x08019BEC and matches the formula to ±1 LSB, so new
firmware can compute it.

## Pitch bend (`pitch_cv_from_note_bend` 0x08018d9c)

`bend` is the 14-bit MIDI value (centre 0x2000), `range` = object+6.

```c
d = abs(bend - 0x2000) * range;          // semitones in 1/8192 units

// V/oct: linear
code = note_code ± (d * 533 >> 13);

// Hz/V: interpolate between adjacent table entries (22-bit precision, >> 10)
semis = d >> 13;  frac = d & 0x1FFF;
n0 = note ± semis;  n1 = n0 ± 1;
code = (HZV(n0)>>10 ± ((frac * (HZV(n1)>>10 - HZV(n0)>>10)) >> 13)) >> 6;
```

Results are clamped to ≥ 0 (bend down) and ≤ 0x1086B = 127 × 533 (bend up), then truncated to
16 bits. With the default base of 0, notes above 122 overflow 16 bits and wrap — a stock-firmware
quirk not worth copying.

## Related finding: bootloader request flag

SysEx command `'&'` (handler 0x080120c4) calls 0x08015c80, which writes the two words
`0xA33A95FF, 0xAAA55A55` to flash at **0x0803F800**. This is most likely the "enter updater on next
boot" flag read by the bootloader, i.e. a way to reach update mode without the button combo (?).
