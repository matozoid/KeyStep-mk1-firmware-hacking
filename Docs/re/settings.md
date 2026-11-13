# Global settings

This document covers the global settings in stock firmware 1.0.28: how they are laid out, their defaults and value ranges, and how they are loaded and saved. It also covers the touch-strip calibration, which is stored in the same flash page. Unconfirmed inferences are marked (?).

## 1. Object and addressing

| Item | Value |
|---|---|
| RAM object | `0x20005058`, size 0xB0, built by the static ctor `settings_ctor` 0x080194e4 |
| Listener list | up to 8 pointers at +0x04..+0x23, count at +0x24, context at +0x28 |
| Persisted block | object +0x2C..+0xAB (RAM `0x20005084`), 0x80 bytes, stored at flash `0x0803F000` |
| Parameter index `p` | `p = object offset − 0x2C`, which is also the byte offset in flash. MCC/SysEx address settings by `p`. |
| Non-persisted | +0xAC test flags (bit0, bit1). There is also +0xA9 test-mode flag: it lies inside the saved block at p=0x7D, so it would be saved if a save happened while in test mode (?). |

Other docs write "settings+0xNN": that is the object offset. In this doc, "code" or "param" means `p`.

### Access functions

| Function | Address | Behaviour |
|---|---|---|
| `settings_set` | 0x080185b8 | `(obj, p, value)`. Clamps the value (§3), stores it at +0x2C+p, then sends event `{1, p, value}` to every listener. |
| `settings_resolve_channel` | 0x080192c4 | Returns the byte at +0x2C+p. If that byte is 0x41 ('A' = "User"), it returns the user channel (+0x42) instead. |
| `settings_broadcast` | 0x0801894a | Sends event `{2}` (full reload) to every listener. |
| `settings_defaults` | 0x08018820 | Writes the defaults (§2). Ghidra has no function at this address. |
| `settings_sanitize` | 0x080187b4 | Repairs a few fields after loading (§4). |

**Event codes.** A single-change event carries the parameter index, so listeners compare it against ASCII letters. For example `'G'` (0x47) means gate type, `'M'` (0x4D) note priority, `'k'` (0x6B) LED brightness, `'!'` (0x21) transpose channel and `'%'` (0x25).

**Channel values.** Channels are stored 0-based (0–15). The value 0x41 means "User", that is, follow the keyboard channel.

## 2. Parameter table

Notes on the columns:
- Def = value set by `settings_defaults`. Bytes it does not write default to 0 (the ctor zero-fills the object).
- Range = the clamp applied in `settings_set` (§3). "—" means no clamp, so any byte is accepted.
- The MCC name comes from the manual, §10.9.

| p | Obj | Def | Range | MCC name / meaning |
|---|---|---|---|---|
| 0x00–0x06 | 0x2C–0x32 | 0 | — | unused (?) |
| 0x07 | 0x33 | 0x41 | ch / 'A' | MIDI Input Channel, used for MIDI→CV. Event 7 resets the CV voice. |
| 0x08 | 0x34 | 0 | bool | MIDI Thru |
| 0x09 | 0x35 | 1 | bool | Sustain Pedal Mode, Gate/Toggle (?) |
| 0x0A | 0x36 | 0x41 | ch / 'A' | Sustain Msg MIDI Ch |
| 0x0B | 0x37 | 0 | — | Sustain Range Low (value sent on release) |
| 0x0C | 0x38 | 127 | — | Sustain Range High (value sent on press) |
| 0x0D | 0x39 | 64 | — | Sustain CC |
| 0x0E | 0x3A | 0 | bool | Hold Switch Mode, Gate/Toggle (?) |
| 0x0F | 0x3B | 1 | 1..3 | Sustain Pedal Function, Hold/Sustain/Both (?) |
| 0x10 | 0x3C | 2 | 1..3 | Hold Switch Function (?) |
| 0x11, 0x12 | 0x3D, 0x3E | 0 | — | unknown (?) |
| 0x13 | 0x3F | 0x41 | ch / 'A' | Mod strip MIDI Ch |
| 0x14 | 0x40 | 1 | — | Mod strip CC |
| 0x15 | 0x41 | 0x41 | ch / 'A' | Pitch Bend MIDI Ch |
| 0x16 | 0x42 | 0 | 0..15 | User Channel (keyboard channel). Mirrored in `0x20000052`. |
| 0x17 | 0x43 | 0x41 | ch / 'A' | "Kbd Play" channel, used only in a special UI state (see keyboard.md) (?) |
| 0x18 | 0x44 | 2 | 0..2 | Keyboard-side flag; keyboard.md reads it as a latch flag (?) |
| 0x19 | 0x45 | 0 | 0..5 | Velocity curve selector (keyboard.md) |
| 0x1A | 0x46 | 0 | 0..4 | Aftertouch curve (keyboard.md) |
| 0x1B–0x1F | 0x47–0x4B | 0 | — | unused (?) |
| 0x20 | 0x4C | 1 | 0..4 | Sync Clock type: 0 1step Gate, 1 1step Clock, 2 Korg 1 pulse, 3 24 PPQ, 4 48 PPQ |
| 0x21 | 0x4D | 15 | 0..16 | Transpose Input channel, 16 = all (?). Event `'!'` resets the transposer to the centre note. |
| 0x22 | 0x4E | 3 | 2..4 | Tap Tempo average (number of taps) |
| 0x23 | 0x4F | 0 | bool | Sync Clock Start: 0 on clock, 1 on gate/start (?) |
| 0x24 | 0x50 | 0 | 0..2 | Next Seq: End / Instant Reset / Instant Continue |
| 0x25 | 0x51 | 1 | bool | Event `'%'` clears UI +0xF; seq-restart flag (?) |
| 0x26 | 0x52 | 1 | bool | Transposition Latch (midi.md) (?) |
| 0x27 | 0x53 | 1 | 1..3 | Transposition Input port, bit mask: 1 USB, 2 DIN, 3 both |
| 0x28 | 0x54 | 0 | bool | Tempo knob mode: Jump / Pickup (?) |
| 0x29 | 0x55 | 0 | bool | Velocity: As recorded / Fixed |
| 0x2A | 0x56 | 100 | ≥1 | Fixed Velocity value |
| 0x2B | 0x57 | 60 | — | Transpose Center Pitch |
| 0x2C | 0x58 | 0 | sanitized ≤1 | sequencer/arp option (?) |
| 0x2D | 0x59 | 0 | sanitized ≤1 | Wait for bar / quantised start under external clock (?) |
| 0x2E | 0x5A | 0 | sanitized ≤1 | Tie convention, copied into new sequences at seq+0x404 |
| 0x2F | 0x5B | 0 | sanitized ≤2 | Arp random algorithm variant (arp_chord.md) |
| 0x30 | 0x5C | 0 | sanitized ≤1 | Record length-append overwrite mode (sequencer.md) |
| 0x31–0x45 | 0x5D–0x71 | 0 | — | unused (?) |
| 0x46 | 0x72 | 0 | bool | Pitch CV output: 0 V/oct, 1 Hz/V |
| 0x47 | 0x73 | 2 | 0..2 | Gate type: 0 S-trig, 1 V-trig 5 V, 2 V-trig 12 V (?) |
| 0x48, 0x49 | 0x74, 0x75 | 0 | — | unused (?) |
| 0x4A | 0x76 | 0 | — | V/oct "0 V note" |
| 0x4B | 0x77 | 0 | — | unused (?) |
| 0x4C | 0x78 | 24 | — | Hz/V "1 V note" (24 = C0) |
| 0x4D | 0x79 | 2 | 0..2 | Note Priority: 0 Low, 1 High, 2 Last (keyboard only) |
| 0x4E | 0x7A | 0 | 0..2 | Mod CV source: 0 mod strip / CC1, 1 velocity (?), 2 aftertouch |
| 0x4F | 0x7B | 5 | 1..12 | Mod CV max, in volts (range in mV = value × 1000) |
| 0x50 | 0x7C | 2 | 1..24 | Pitch bend range in semitones. Also settable via RPN 0. |
| 0x51–0x5F | 0x7D–0x8B | 0 | — | unused (?) |
| 0x60 | 0x8C | 1 | 0..3 | Transport Mode: bit0 MIDI CC, bit1 MMC |
| 0x61 | 0x8D | 0x41 | ch / 'A' | Stop channel |
| 0x62 | 0x8E | 0x41 | ch / 'A' | Record channel |
| 0x63 | 0x8F | 0x41 | ch / 'A' | Play channel |
| 0x64 | 0x90 | 51 | — | Stop CC (?) |
| 0x65 | 0x91 | 50 | — | Record CC |
| 0x66 | 0x92 | 54 | — | Play CC (?) |
| 0x67 | 0x93 | 0 | bool | Play/Pause button = Pause or Reset (?) |
| 0x68–0x6A | 0x94–0x96 | 0 | bool | unknown flags (?) |
| 0x6B | 0x97 | 100 | 10..100 | LED brightness in %. Event `'k'`. |
| 0x6C–0x7F | 0x98–0xAB | 0 | — | unused, apart from 0x7D = +0xA9 test mode |

## 3. Clamp rules in `settings_set`

| Rule | Codes |
|---|---|
| Channel or 'A': keeps 0x41, anything else is capped at 15 (0x08019900) | 0x07, 0x0A, 0x13, 0x15, 0x17, 0x61, 0x62, 0x63 |
| 0..15 | 0x16 |
| Boolean (any non-zero becomes 1) | 0x08, 0x09, 0x0E, 0x23, 0x25, 0x26, 0x28, 0x29, 0x46, 0x67–0x6A |
| 1..3 | 0x0F, 0x10, 0x27 |
| ≤2 | 0x18, 0x24, 0x47, 0x4D, 0x4E |
| ≤4 | 0x1A, 0x20 |
| ≤5 | 0x19 |
| ≤16 | 0x21 |
| 2..4 | 0x22 |
| ≥1 | 0x2A |
| 1..12 | 0x4F |
| 1..24 | 0x50 |
| ≤3 | 0x60 |
| 10..100 | 0x6B |
| No clamp | all other codes, including 0x2C–0x30. These are only fixed by `settings_sanitize` at boot. |

Clamping saturates to the nearest limit; it does not reject the value.

## 4. Boot / load

Boot order in `main`:

1. If Oct− and Oct+ are held, or the sequence flash is empty, `seq_flash_erase_all` runs (§6).
2. `touchstrip_cal_load` 0x08015eb8 (§7).
3. `settings_load` 0x08015cd0. If the first word at `0x0803F000` is not 0xFFFFFFFF, it copies 0x80 bytes into `0x20005084`. There is **no checksum, version or magic number**: an erased first word is the only validity test. Otherwise the ctor defaults stay in place.
4. `settings_sanitize` 0x080187b4 repairs these fields:

| Obj | Bad if | Reset to |
|---|---|---|
| 0x58, 0x59, 0x5A, 0x5C | > 1 | 0 |
| 0x5B | > 2 | 0 |
| 0x97 | outside 10..100 | 100 |
| 0x4D | > 16 | 15 |

5. The user channel (+0x42) is copied to `0x20000052`.
6. `settings_broadcast` sends `{2}`, and every module re-reads its fields.

A firmware with a different layout would inherit these bytes from the stock firmware. Consider adding a magic/version word if we keep page `0x0803F000`.

## 5. Saving

The flash manager object at `0x20001AB8` holds a 2 KB page buffer at +0x000. Its other fields:

| Offset | Content |
|---|---|
| +0x800 | busy flag |
| +0x802 | current half-word index |
| +0x804 | last half-word index |
| +0x808 | target page address |
| +0x80C | dirty mask: bits 0–7 = sequences 1–8, bit 8 = settings, bit 9 = calibration |

### Who marks settings dirty

| Trigger | Path | Delay |
|---|---|---|
| SysEx set (`02 00 41 …`) | sets flag `0x20000069`. The main loop checks it when the 1 s countdown `0x2000006A` (reload 20) expires, then calls `settings_mark_dirty` 0x08016060. | ≤20 s |
| User channel changed on the panel (Shift+key) | `0x080142f0` calls `settings_set(0x16)`, then `settings_mark_dirty` right away | immediate |
| Seq number change on Stop | see sequencer.md | — |
| Touch-strip calibration finished | `touchstrip_cal_mark_dirty` 0x08015f24 sets bit 9 | immediate |

The 1 s base comes from SysTick: the counter at `0x20000040` counts to 999 ms, then the countdowns at `0x2000006A` and `0x2000006C` are decremented. Sequence edits from SysEx use `0x2000006B` / `0x2000006C` with reload 10, so they are saved 10 s after the last edit.

### Write sequence (`flash_worker` 0x08015f34, called every main-loop pass)

1. When idle, the worker takes one dirty bit:
   - a sequence: 0x08015b90, page from 0x08015b08, 0x203 half-words;
   - settings: 0x08015c68 calls `flashpage_write_merge(mgr, 0x000, 0x20005084, 0x80)`;
   - calibration: 0x08015e20 calls `flashpage_write_merge(mgr, 0x200, cal[4], 4)`.
2. `flashpage_write_merge` 0x080160f8:
   - copies the whole 2 KB page at `0x0803F000` into the buffer and overlays the new bytes at the offset;
   - sets the end index to 0x3FF half-words;
   - unlocks flash (keys 0x45670123 / 0xCDEF89AB, 0x08005f4c);
   - starts an **interrupt-driven page erase** (0x08015dc8 → HAL erase-IT 0x08005c2c).
   - 0x08015dc8 checks the target is inside 0x0803B000..0x0803F000. Otherwise it stops in the fatal LED blink 0x0800c010 with code 3.
3. After the erase interrupt, state 1 programs **32 half-words per main-loop pass** with HAL program-IT (0x08005ed0) until the end index.
4. State 2: the dirty bit is cleared and flash is locked (0x08005e08). Any other state calls fatal blink code 2.

Because settings and calibration share one page, saving either one rewrites both. If power is lost between the erase and the end of programming, both are lost. The settings then load as defaults because the first word is 0xFFFFFFFF, and the calibration falls back to 0xBF/0x09.

## 6. Factory reset

`seq_flash_erase_all` 0x08015bd0 runs when Oct− and Oct+ are held at power-up, or when all 8 sequence pages are blank. It works synchronously (blocking HAL calls):

1. Erases the 8 sequence pages `0x0803B000..0x0803EFFF`.
2. Rewrites the default sequences (0x08015cf4) (?).
3. `0x08016024`:
   - snapshots the `0x0803F000` page into the buffer;
   - overwrites the first word of the **buffer** with 0xFFFFFFFF. It does this by calling the HAL program routine on a RAM address, which works because it is a plain store.
   - erases the page and programs all 0x200 words back.

Result: settings return to defaults on this same boot (load runs afterwards), while the touch-strip calibration at +0x200 survives.

## 7. Touch-strip calibration (`0x0803F200`)

| Byte | Meaning | Default if erased |
|---|---|---|
| 0x0803F200 | Pitch strip calibration A (?) | 0xBF |
| 0x0803F201 | Pitch strip calibration B (?) | 0x09 |
| 0x0803F202 | Mod strip calibration A (?) | 0xBF |
| 0x0803F203 | Mod strip calibration B (?) | 0x09 |

- **Load** (`touchstrip_cal_load` 0x08015eb8): if byte 0x0803F200 is 0xFF, the defaults are used for both strips. Otherwise bytes 0/1 go to the pitch strip object `0x200000B0` and bytes 2/3 to the mod strip object `0x200000B4`, through setters 0x080111e2 / 0x080111de.
- The two values are probably the raw max/min ADC bytes of the strip (0xBF ≈ top, 0x09 ≈ bottom) (?).
- **Calibration procedure**: driven over SysEx by MCC (sysex.md §5.6). When both strips report done (0x080111e6), 0x08015f24 marks bit 9 dirty and the page is rewritten as in §5.

## 8. Other flash locations

| Address | Use |
|---|---|
| 0x0803B000–0x0803EFFF | 8 × 2 KB sequence pages |
| 0x0803F000–0x0803F07F | settings |
| 0x0803F200–0x0803F203 | touch-strip calibration |
| 0x0803F800 | bootloader request flag page: words 0xA33A95FF, 0xAAA55A55 (sysex.md §8) |
