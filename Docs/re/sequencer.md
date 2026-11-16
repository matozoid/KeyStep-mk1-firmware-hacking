# Stock sequencer (reverse-engineered)

Source: stock app image (0x08004000), analysed with `vendor/ghidra_b`. All descriptions are our
own paraphrase; `(?)` marks inferences that are not fully confirmed.

## 1. Sequence storage

### 1.1 Layout (0x408 bytes per sequence)

| Offset | Size | Meaning |
|---|---|---|
| 0x000 + s*0x10 | 16 | Step s (0..63): 8 × (note, velocity) pairs |
| 0x400 | 1 | Length, 1..64 |
| 0x401 | 1 | Swing %, 50..75 |
| 0x402 | 1 | Gate %, 1..99 |
| 0x403 | 1 | Flags: bit0 = "cleared placeholder" (length 1, step 0 = rest); bit1 = dirty (needs flash save) |
| 0x404 | 1 | Tie convention, copied from settings+0x5a at clear time (see 1.3) |
| 0x405..0x407 | 3 | Unused / padding (?) |

Note byte values in a slot:

| Value | Meaning |
|---|---|
| 0x00..0x7F | MIDI note |
| 0x81 | Tie/continue marker: no new note-on; at note-off the gate is held (?) |
| 0x82 | Rest |
| 0xFF | Empty slot / end of the chord list |

Velocity byte: bits 0..6 = velocity. **Bit 7 of slot 0's velocity (byte 1 of the step)** is the
step's tie/legato flag.

### 1.2 RAM

- 8 sequence buffers at 0x200022C8 + n*0x408, pointer table at 0x20005038[8].
- A 9th buffer at 0x20004308 is a scratch sequence that the arpeggiator fills with its pattern; seq
  and arp share the same note player.
- On boot each sequence is copied from flash (0x102 words) into RAM.

### 1.3 Tie convention (+0x404)

| Value | Where the tie bit lives | Playback check |
|---|---|---|
| 1 | On the step that *starts* the tied note | Note-off of step s looks at step s |
| 0 | On the step that *continues* the note | Note-off of step s looks at step s+1 |

This is the MCC "Tie Mode" setting (0 Tie to Previous, 1 Tie to Next). It also changes what Tap
does in step record (3.1).

### 1.4 Defaults

- Clear sequence: every step note0 = 0x82 (rest), velocity 100; length 0 then grows while recording.
- Rest written by Tap: 0x82 with velocity 0x40.
- Clear-last down to < 2 steps: length 1, step 0 = rest, placeholder flag set.
- Scratch/arp buffer init: step 0 rest 0x82/0x40, all other slots 0xFF/0x40, swing 50, gate 50.
- Factory sequences: written to flash on "erase all" from a token stream of halfwords
  (0xA5A2 = header/step 0x40, 0xA5A3 = next sequence, 0xA5A1 = next step, 0xA5A0 = skip,
  0xA5A4 = end, anything else = data). The settings page is then invalidated so it re-defaults.
- A second template builder (lengths at 0x0801A272, note-pointer table 0x200001F8, flag table
  0x20000218 with 1 = rest, 2 = tie) is reachable from the seq/arp switch after pressing Shift
  3+ times within a short timer window, i.e. a hidden "load demo pattern" gesture (?).

### 1.5 When it is written to flash

- Flash slot for sequence n: 0x0803B000 + n*0x800 (one 2 KB page each).
- Every edit sets the dirty bit (hdr+0x403 bit1).
- The main loop runs a debounce counter (0x2000006C, flag 0x2000006B). When it expires, all
  dirty sequences move to the flash manager's pending mask and their dirty bit is cleared. Leaving
  record mode (Stop handler) also triggers this collection immediately.
- Flash manager (object 0x20001AB8, 0x800 byte page buffer) pending mask: bits 0..7 = sequences,
  bit 8 = global settings, bit 9 = touch-strip calibration.
- Per item: copy 0x408 bytes to the buffer, start an interrupt-driven page erase; the FLASH IRQ
  advances the state; then 32 halfwords are programmed per main-loop pass; when done the pending
  bit is cleared. Non-blocking, so playback continues during saves.
- Settings saves use a separate debounce (0x20000069/0x2000006A, reload 20).
- Possible quirk: the idle branch copies *each* pending sequence into the buffer in one loop, so
  the buffer ends up with the last one while the target address belongs to the first (?). Needs
  a closer look before relying on it.

## 2. Engine

### 2.1 Timebase

- Internal resolution **480 PPQN**. TIM2 is the tick timer:
  `ARR = 3.6e9 / (bpm100 × 48) − 1`, bpm100 = BPM × 100 (default 12000). With a 6 MHz timer clock
  this gives `tick_rate = BPM × 8 Hz` (?: timer clock inferred from the formula).
- A second timer (handle 0x20000F08) counts ticks inside the loop: CNT = position,
  ARR = len × T − 1 (?).
- The engine routine runs 3× per main-loop iteration and derives everything from the position.

### 2.2 Time division (ticks per step, T)

| Index | T | Division |
|---|---|---|
| 0 | 480 | 1/4 |
| 1 | 240 | 1/8 |
| 2 | 120 | 1/16 (default) |
| 3 | 60 | 1/32 |
| 4 | 320 | 1/4T |
| 5 | 160 | 1/8T |
| 6 | 80 | 1/16T |
| 7 | 40 | 1/32T |

A division change is queued and applied only when the global tick counter (0x20000090) is a
multiple of the new T, so the grid stays phase-aligned. The knob position is the index itself
(timediv_knob_process 0x0801307c; detent windows in hardware_map.md).

### 2.3 Step, swing, gate

`step = position / T` (except in sync-in "1 step" mode, where gate edges on Sync In drive the step:
rising edge = note on, falling edge = note off + advance).

Swing (`s` = hdr+0x401, 50..75):

- `pairLen = 2T · s / 100` is the length of the first step of each pair.
- A phase bit toggles on every step change and is reset on restart; it carries over the loop
  boundary, so odd lengths alternate which step swings (?).
- With s > 50, the even (phase 0) step is unchanged and the odd step is delayed to
  `step·T + (pairLen − T)`.

Gate-off tick (`g` = hdr+0x402):

| Case | Gate-off |
|---|---|
| No swing (s ≤ 50) | `start + (T−1)·g/100`, clamped to `start+T−1`, at least `start+15` |
| Delayed (late) step | `delayedStart + (stepEnd − delayedStart)·g/100` |
| Lengthened (early) step | `start + pairLen·g/100`, clamped to the loop end |

### 2.4 Multi-note steps, ties and legato (note player, object 0x20001720)

- Up to 8 notes per step. Slots with 0x81/0x82 are skipped for note-on.
- Note-on of a step:
  - If not tied, all previously sounding notes go off first.
  - If tied (tie bit per convention 1.3, or the pending flag left by the previous note-off), notes
    present in both steps are not re-triggered (true tie) and old notes not in the new step are
    released *after* the new notes start (legato overlap).
- Note-off of a step: if the next step (convention 0) or this step (convention 1) carries the tie
  bit or a 0x81 marker, gate and notes are held and a "tie pending" flag is set; otherwise all
  notes off (note-off velocity 0x40) and the gate goes low.
- Velocity = stored & 0x7F, or the fixed value settings+0x56 when settings+0x55 is set.
- In keyboard chord mode the chord intervals (0x20005018) are added to each played note.
- CV/Gate follows the same events.

### 2.5 Transpose

- `out = note + (transpose − centre)`, centre = settings+0x57; result folded by octaves into 0..127.
- Current transpose key at 0x20000058; −1 = none. Reset to the centre when entering seq mode,
  when changing sequence and when entering record.
- Set from the keyboard while playing (and from MIDI input on a configurable channel/port,
  settings+0x4D channel, +0x53 port mask).

### 2.6 Next Seq (settings+0x50)

| Value | Behaviour |
|---|---|
| 0 End | Queued sequence starts when the current loop wraps to step 0 |
| 1 Instant Reset | Switch at the next step boundary, restart from step 0 |
| 2 Instant Continue | Switch at the next step, position = global tick mod (newLen·T) |

Length changes made while playing are applied at a step boundary as well (gated by
settings+0x59 / 0x20000063 (?)).

### 2.7 Transport

Clock object 0x20001780; transport command codes:

| Cmd | Action |
|---|---|
| 0 Stop | Player off, MIDI 0xFC, counters reset, sync lines low |
| 1 Start | MIDI 0xFA, counters 0 |
| 2 Pause | MIDI 0xFC, paused flag |
| 3 Continue | MIDI 0xFB, resume at position mod loop length |
| 4 Resync | Restart at step 0 without stopping |

- MIDI real-time messages are sent only when the clock source is internal (0) or sync-in (2), not
  when slaved to MIDI/USB.
- Transport MMC/CC messages are sent when settings+0x8C bit1 is set.

Button logic (UI handler 0x080142f0, transport state UI+0x0D: 0 stopped, 1 paused/armed, 2 playing):

| Button | Behaviour |
|---|---|
| Play/Pause from stop | Start (cmd 1). If slaved to external clock and not running, arms instead (state 1) |
| Play/Pause while playing | Pause (cmd 2); if settings+0x93 ("Play/Pause = Reset" option (?)) and running on internal clock, resync to step 0 (cmd 4) instead |
| Play/Pause while paused | Continue (cmd 3), or fresh start (cmd 1) if the pause came from a fresh arm |
| Stop | Stop (cmd 0), ends record, triggers flash save of dirty sequences, saves the changed seq number to settings |
| Shift + Play | Resync / restart (cmd 4) |
| Shift + Stop | Delete last recorded step (step record only) (?: button id 6 inferred as Stop) |
| Rec | Toggle record state UI+0x10 (0 off, 1 recording, 2 armed, blinking). Acts on release, and only if no length was entered (2.8) |
| Rec held + channel keys | Set the sequence length (2.8) |

### 2.8 Length entry with Rec held

- Rec press (button handler 0x080142f0, at 0x08014866) sets the flag 0x20000060 and, in Seq mode,
  lights the Rec LED. Record is not toggled yet.
- Key down while the flag is set (0x08017520): the key makes no sound. In Seq mode, or in Arp mode
  with the Random mode and "Arp Random" = Pattern, the 16 lowest keys (matrix index 16..31) add
  `32 − index` to the byte 0x20000061: lowest key 1, sixteenth key 16. The sum is capped at 64.
  Other keys add nothing.
- Rec release (0x08014ac8) clears the flag. If the sum is non-zero:
  - Seq mode: `seq_set_length_pad` (0x08015354) with the sum. If record was just armed and nothing
    was entered yet, the sequence is cleared first, so this gives an empty sequence of that length.
    New steps are padded with rests: all of them when settings+0x5C is 1 (clear), only the steps
    that hold no note when it is 0 (keep). Shortening never erases anything by itself. The record
    state stays as it was and the Rec LED is restored.
  - Arp Pattern random: the sum becomes the loop length (arp_chord.md).
  - The sum is reset to 0.
- If the sum is zero, the release does the normal record toggle.

## 3. Recording

Recorder state lives in the sequence object at 0x200016E8 (step-record buffer, note-on/off
counters, Tap state, real-time buffer, current/next sequence pointers).

### 3.1 Step record (transport stopped)

- Pressed keys accumulate into a chord buffer (max 8, duplicates ignored); note-offs are counted.
- When the last key is released, the chord is written to step `len` and `len++`, marking dirty.
  On the first input after arming, the sequence is cleared (length 0, tie convention re-copied).
  If the placeholder flag is set it is cleared and the length restarts at 0.
- Velocity is stored as played (fixed velocity applies at playback only).
- Tap (0x08015794, with the key handler 0x080151c0):
  - Tap pressed with no key held → "rest pending". On release a rest (0x82/0x40) is written and
    `len++`. If a chord was entered while Tap was held, no rest is written.
  - Tap held while the last key goes up (legato entry) → the chord is written with the tie bit,
    the next step is preset to rest, `len++`. Same in both tie conventions.
  - Tap with keys held, convention 0 (Tie to Previous): nothing on press. On release the chord is
    written with the tie bit and `len++`. Releasing the keys then writes nothing. N taps give
    N steps, all with the tie bit, so the first one also ties to the step before it.
  - Tap with keys held, convention 1 (Tie to Next): on press the chord is written with the tie
    bit, the next step is preset to rest and `len++`. Nothing on release. Releasing the keys
    writes the last step without the tie bit. N taps give N + 1 steps.
  - Tap pressed with no key, then a key pressed and still held when Tap goes up: a rest is
    written first, the chord follows as a normal step when the key goes up.
  - Stock bug: with `len` = 63 the "preset next step" write lands on offset 0x400 (the length
    byte) before `len++`.
- Recording stops at 64 steps.
- Delete last step: `len−1`; below 2 it resets to the placeholder (1 rest step).

### 3.2 Real-time record (playing)

- Held notes (max 8) are collected; a note-off removes the note from the buffer.
- At each step the buffer is copied into the step under the play head (**replace**, not overdub).
  When the same held chord spans several steps, the following steps get the tie bit instead of a
  re-attack.
- Tap during real-time record writes a rest (0x82/0xFF) or, with keys held, a tie marker
  (0x82/0x7F, internal count 9).
- Quantisation: the position is rounded to the nearest step (`(pos + T/2) / T`), so notes played up
  to half a step early land on the next step.
- Append: when changing length with the length control, new steps are padded with rest 0x82/100.
  With settings+0x5C == 1 every new step is overwritten; otherwise only steps whose note is already
  ≥ 0x80 (rest/empty) are padded, so previously recorded data beyond the old length survives (?).

## 4. Clock core

- Tempo: BPM×100 stored at clock+0x0E, applied via the TIM2 formula above.
- Clock source (clock+0x0C): 0 internal, 1 MIDI/USB (?), 2 Sync In.
- MIDI clock out: 0xF8 every 20 ticks (24 PPQN); the Tap LED (bit 6) is lit for the first 12 clocks of each beat and dark for the other 12.
- Sync Out pulse spacing (settings+0x4C), 50 % duty:

| Setting | Ticks per pulse | Notes |
|---|---|---|
| 0 1 step Gate | 20 | Sync Out mirrors the sequencer gate |
| 1 1 step Clock | 120 | One pulse per 1/16 |
| 2 Korg | 240 | Short pulse |
| 3 24 PPQ | 20 | |
| 4 48 PPQ | 10 | |

- External sync: each incoming pulse is expanded to `ticksPerPulse − alreadyInterpolated + 1`
  ticks, so the internal 480 PPQN keeps running between pulses and catches up.
- Shift + tempo knob (`tempo_knob_process` 0x080134d8, Shift flag 0x2000005F, internal sync only):
  the first knob change with Shift held only stores the current tempo rounded down to a whole BPM
  (knob object +0x30) and sets the flag +0x29. Each later change sets
  `tempo = base + ((v − 127) × 1000 / 12700) × 100`, v = 8-bit knob position, integer division,
  clamped to 30.00..240.00. That is whole BPM steps, −10..+10 around the base, knob centre = base.
  The flag is cleared only by a knob change without Shift, so releasing Shift and holding it again
  keeps the same base. The pickup state is not touched.
- Tap tempo: a period is measured and converted to BPM; the tempo knob pickup accepts the range
  30.00..240.00 BPM. Averaging depth is settings+0x4E (2..4 taps, per the clamp rules in settings.md). Jump vs. Pickup is
  probably settings+0x54 (?). Not decoded in detail.

## 5. Settings bytes used by the sequencer

Offset = settings base 0x20005058 + 0x2C + SysEx parameter id (?).

| Offset | Meaning |
|---|---|
| +0x4C | Sync Out type |
| +0x4D | Transpose input channel (16 = all) |
| +0x4E | Tap tempo average, 2..4 taps |
| +0x50 | Next Seq mode |
| +0x51 | Record/restart related flag (?) |
| +0x53 | Transpose input port mask: 1 USB, 2 DIN, 3 both |
| +0x54 | Tempo knob mode: Jump / Pickup (?) |
| +0x55 / +0x56 | Fixed-velocity enable / value |
| +0x57 | Transpose centre note |
| +0x59 | MCC "Arm to Start"; wait-for-bar / quantised start under external clock (?) |
| +0x5A | MCC "Tie Mode" (Tie to Previous / Next), copied into new sequences |
| +0x5C | MCC "Sequence Length Update" (keep / clear content beyond length); length append overwrite mode |
| +0x8C bit1 | Send transport messages |
| +0x93 | Play/Pause acts as reset (?) |

Defaults and clamp ranges are in [settings.md](settings.md).

## 6. Open questions

- Tap-tempo averaging and Jump/Pickup (around 0x080123b8–0x080134d8, object 0x200017E0).
- Exact meaning of settings+0x51, +0x59, +0x93 and the 0x20000063 flag.
- The flash-writer buffer quirk in 1.5.
- Role of 0x81 in stored data (written by the template loader for "tie" entries?).
