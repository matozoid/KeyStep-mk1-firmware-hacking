# Arpeggiator and Chord mode (stock fw 1.0.28)

Reverse-engineered from the app image at 0x08004000. Addresses are flash/RAM
addresses. Unconfirmed inferences are marked (?).

## Architecture in one paragraph

The arpeggiator does not play notes itself. It is a *pattern generator*: from
the held-note list it writes a step pattern into a RAM buffer
(0x20004308, pointed to by 0x20000108) that has exactly the same 0x408-byte
layout as the eight sequences. The ordinary sequence player
(0x200016E8 via 0x200000F0) then plays that buffer on the shared clock
(0x20001780 via 0x200000EC). So arp swing/gate/tie/velocity handling is the
sequencer's handling.

Pattern buffer layout (shared with sequences):

| Offset | Meaning |
|---|---|
| step*16 + slot*2 | (note, velocity), 8 slots per step |
| +0x400 | length (1..64) |
| +0x401 | swing % |
| +0x402 | gate % |
| +0x403 bit1 | dirty / rebuild flag |

Note 0x82 = empty step, 0x81 = special (rest or tie (?)). Velocity bit 7 on
slot 0 = tie to next step.

## Clock

- SysTick runs at 10 kHz (not 1 kHz).
- Internal resolution 480 ticks per quarter note. MIDI clock out is
  position/20 = 24 PPQN.
- Step length by time-division index (0x0800e820):

| Idx | Ticks | Division |
|---|---|---|
| 0 | 480 | 1/4 |
| 1 | 240 | 1/8 |
| 2 | 120 | 1/16 (default) |
| 3 | 60 | 1/32 |
| 4 | 320 | 1/4T |
| 5 | 160 | 1/8T |
| 6 | 80 | 1/16T |
| 7 | 40 | 1/32T |

Clock is constructed with 12000 (= 120.00 BPM (?)) and index 2.

## Arp object (0x20001A8C, via 0x20000100)

| Offset | Field |
|---|---|
| +0x00 | RNG state (u32) |
| +0x04 | RNG seeded flag |
| +0x06 | hold (latch) flag |
| +0x08 | held-note list |
| +0x0c | "released while latched" list |
| +0x10 | mode 0..7 |
| +0x11 | random-walk position |
| +0x12 | random pattern length (default 16) |
| +0x13 | current octave offset (0xFF = reset) |
| +0x14 | octave range, signed −3..+3 |

### Note list class (0x81 bytes)

Count at +0, up to 32 (note, velocity) pairs at +1 in insertion order, a sorted
index at +0x41. Sorting is a simple selection sort by pitch; equal pitches keep
insertion order. Adding ignores duplicates and silently drops notes beyond 32.

## Note order per mode (0x0800d850)

n = number of held notes, s[0..n−1] = ascending pitch, o[] = insertion order.

| Mode | Name | Pattern | Length |
|---|---|---|---|
| 0 | Up | s0 s1 … s(n−1) | n |
| 1 | Down | s(n−1) … s0 | n |
| 2 | Inclusive | s0 … s(n−1) s(n−1) … s0 (both ends repeated) | 2n |
| 3 | Exclusive | s0 … s(n−1) s(n−2) … s1 (ends not repeated) | 2n−2 |
| 4 | Random | see below | |
| 5 | Order | o0 o1 … o(n−1) (play order) | n |
| 6 | Up×2 | s0 s0 s1 s1 … (each note twice) | 2n |
| 7 | Down×2 | s(n−1) s(n−1) … s0 s0 | 2n |

- Up×2 / Down×2 do **not** double octaves; they repeat each note on two
  consecutive steps.
- Length is capped at 64 steps.
- n = 0: pattern = one empty step, octave reset.
- n = 1: one step (except Random variant 1, which still builds its pattern).
- Rebuilds are requested on note/mode/range change and applied by the clock at
  the pattern loop point (step 0) while running, or immediately when stopped.

### Random

RNG (0x0800d7d4): 32-bit LCG `x = x*0x0BB38435 + 0x3619636B`, seeded once from
the clock position on first use. Index pick = `((x>>27)+1) mod n`; random
octave / walk direction use bits 20–21.

The random algorithm has three variants, selected by settings byte
0x20005058+0x5b (p 0x2F, MCC "Arp Random": 0 Total, 1 Pattern, 2 Brownian; default 0):

| Variant | Behaviour |
|---|---|
| 0 | One-step pattern containing a random held note, rebuilt every step. Octave is also randomised each loop (see below). |
| 1 | A fixed random pattern of length +0x12 (default 16): each step = random note + random octave 0..3 (sign follows range; 0 if outside range; plain note if > 127). Regenerated only when notes or octave range change → a repeating "random" riff. |
| 2 | Pattern is built in Order; playback position does a random walk: bits 20–21 → 0: step back, 1: repeat, 2/3: step forward (wrapping). |

Variant 1 length can be edited: in Random variant 1 (or Seq mode) the key
handler adds `32−key` to 0x20000061 (capped at 64) (?).

## Octave handling (0x0800de98)

Called at every pattern wrap and on transport start. With range R:

- R > 0: octave offset cycles 0, 1, …, R, 0, …
- R < 0: cycles 0, −1, …, R, 0, …
- R = 0: stays 0.
- Random variant 0: random 0..3 (negated if R < 0), forced to 0 if outside R.
- Random variant 1: stays 0 (octave baked into the pattern).

At note-on in Arp mode the player adds 12 × offset to the step note, then the
global transpose (0x20000058).

Range control: in Arp mode Shift+Oct−/Oct+ steps the arp octave object
(0x200016D0, limit ±3); pressing both resets to 0. The keyboard octave object
(0x200016C4) has limit ±4. Changing the range resets the cycle to 0.

## Hold / latch rules

- Hold is forced off while transport is stopped (setter 0x0800d660).
- Note-off with hold off, or transport stopped: note removed.
- Note-off with hold on: note stays, and is added to the "released" list.
- Note-on with hold on: if *every* held note has been released
  (count(released) == count(held)), the whole latched set is cleared first,
  so a new chord replaces the old one. Otherwise the new note is added to the
  latched set (legato add), up to 32 notes, duplicates ignored.
- Hold turned off: all released notes are removed; keys still physically down
  remain.
- Stop (0x0800de68): both lists cleared, octave reset, pattern emptied.
- The hold flag comes from the Hold button/pedal controller (0x20004710), whose
  function mask can also send sustain CC instead.

## Gate and swing

Set with Shift + keyboard key. Handler 0x08018344, key index k counts from the
top key (k = 0 → key 32). Values go to the pattern of the active player
(0x200000F0), i.e. the arp buffer in Arp mode.

| Keys | k | Function | Values (key 22 → 32 / key 17 → 21) |
|---|---|---|---|
| 22–32 | 10..0 | swing % (table 0x0801a0a2) | 50 (Off), 53, 55, 57, 59, 61, 64, 67, 70, 73, 75 |
| 17–21 | 15..11 | gate % (table 0x0801a0ad) | 10, 25, 50, 75, 90 |
| 1–16 | 31..16 | MIDI channel (not arp) | |

Arp init sets swing 50, gate 50.

Timing (clock tick 0x0800ea2c), L = step ticks, S = swing %, G = gate %:

- Swing only when S > 50: every second step is delayed by `2·L·S/100 − L`
  ticks.
- No swing: note-off at `start + (L−1)·G/100`, clamped to
  [start+15, start+L−1].
- Swung pair: on-beat step gate = `(2·L·S/100)·G/100`; delayed step gate =
  `(stepEnd − delayedStart)·G/100`.

## Chord mode

UI state 0x20001AA4 (+0xe): 0 = off, 1 = on, 2 = capturing.

### Enter / exit (0x080141e4, 0x080142f0)

- Press Shift+Hold: old state saved in +0x5, Hold LED fast blink, state = 2.
- Release: if any note was captured (flag 0x20000066) **or** chord was off
  before → chord on (state 1, slow blink). If chord was on and nothing was
  played → chord off.

### Capture (keyboard_process note-on, ~0x801739e)

Table 0x20005018: 16 × (note, velocity), 0xFF-terminated; entry 0 is the
root. Default after boot: {60} only. Not saved to flash.

- First key in a capture session clears the table (all notes 0xFF, velocity
  0x40). Count lives in keyboard object +0x254.
- Each key (up to 16) is converted to its MIDI note with the current keyboard
  octave; duplicates are ignored.
- If the new note is lower than entry 0, the old root moves to the new slot and
  the new note becomes entry 0. So the **root is the lowest captured note**,
  not the first one as the manual says.
- Captured notes are also sounded normally. Shift is still held during capture, so in this state
  keys must not act as Shift + key functions (channel, gate, swing). The open firmware first
  got this wrong: every captured key changed a setting and no chord was stored.
- Velocity is **not** captured: entries keep 0x40; playback uses the played
  key's velocity anyway.

### Playback / transposition

Expansion function 0x0800fedc: `out = played + (chord[i] − chord[0])`, folded
by octaves into 0..127.

- Keyboard (not in Arp mode): each key plays all chord entries with the key's
  velocity. Sounding notes are recorded at keyboard +0x256 (16 bytes) and
  released with velocity 0x40 at key-up.
- Arp + chord: the arp sees only the played keys. At each arp step note-on
  (0x080102a0) the arp note is expanded with chord entries 1..15 at the same
  velocity, and the extra notes are stored for note-off. So each arp step
  becomes a full chord rooted on the arp note (after octave offset).
- Seq: chord notes go into the sequence like ordinary multi-note steps (?).

## UI state fields (0x20001AA4)

| Offset | Meaning |
|---|---|
| +0x05 | chord state saved before capture |
| +0x0c | 0 = Arp, 1 = Seq |
| +0x0d | transport: 0 stopped, 1 paused, 2 playing |
| +0x0e | chord: 0 off, 1 on, 2 capturing |
| +0x0f | Seq transpose mode (?) |
| +0x10 | record state 0/1/2 |

## Other

- Step note-on: velocity from the pattern, or fixed velocity
  (settings+0x56) when settings+0x55 ≠ 0. Output channel = keyboard channel
  (0x20000052), to both USB and DIN; CV follows unless Seq transpose is active.
- hardware_map.md lists SysTick as 1 kHz; it is 10 kHz.
