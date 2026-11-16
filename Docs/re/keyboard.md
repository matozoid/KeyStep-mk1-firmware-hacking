# Keyboard, strips, pedal: stock behaviour (fw 1.0.28)

This doc is the behaviour spec for the open firmware. It comes from the Ghidra project `vendor/ghidra_c` (`keystep`) and objdump.
- Addresses are absolute flash addresses (app image at 0x08004000).
- `settings` is the object at 0x20005058. Parameter id N lives at `settings+0x2C+N`. Getter 0x080192c4, setter 0x080185b8.
- For channel parameters, the value 0x41 ('A') means "follow the user channel".
- (?) marks inference or uncertainty.

## 0. Timebase correction

SysTick runs at **10 kHz**, not 1 kHz as hardware_map.md says. SystemClock_Config loads HCLK/10000.
- 0x20000070 is a free-running 100 µs tick counter. It is the clock for velocity timing.
- The HAL 1 ms tick comes from a divide-by-10 inside the handler.

## 1. Key scan

`keyboard_process` (0x08016f78) runs once per pass of the free-running main loop (app_main 0x0800c1f4). It is not timer driven, so the scan rate is whatever the loop achieves (?). Velocity resolution is 100 µs no matter how fast the scan runs, because it is a timestamp difference.

### Matrix and note numbers
- There are 4 groups of 8 keys. Group g reads matrix line 2g (first contact, "c1") and line 2g+1 (second contact, "c2"). Readers: 0x08016f3c / 0x08016f4e → 0x08011b4a.
- Key index k runs 0..31. Note = 72 − k + octave·12 (+ transpose, see 4). So k=0 is the top key (C, 72 at octave 0) and k=31 is the bottom key (41). The matrix driver (select 0x080165ea drives PE(8+line) low, read 0x080165b8 returns the inverted PD8–PD15) and the indexing k = 8g + bit are the same in the open firmware, so it maps every key to the same note as stock. Confirmed on hardware with the open firmware: the keys send 41 (leftmost) to 72 (rightmost) at octave 0, so k=0 is the rightmost key.
- The note-on stores the octave offset (kb+0x24+k) and the MIDI channel (kb+0x267+k) for that key. The matching note-off uses those stored values, so an octave or channel change while a key is held never strands notes.
- A computed note below 0 is skipped (octave −4 on the lowest keys). The top is not clamped: the highest is 72+48 = 120.

### Per-key state machine
The state byte is at kb+0x44+k:
- bits 2–3: the previous (c1,c2) pair;
- bit 4: the note is sounding.

| Previous | Now | Action |
|---|---|---|
| idle | c1 only | Record start tick (kb+0x64+4k). |
| idle | c1+c2 in the same scan | Note-on, velocity 127. |
| c1 | c1+c2 | Note-on, velocity = curve(now − start). |
| c1 | open | Abort, no note. |
| sounding | both open | Note-off `0x80|ch, note, 0x40`. |
| sounding | c1 only | Nothing. The note-off waits for c1 to open too. |

- Note-on is sent as `0x90|ch`. Release velocity is always 64.
- kb+0xe4 is the bitmap of keys currently down.
- First key down while no other key is down: 0x200000D9 is set to 2, which blocks aftertouch for the next 2 AT periods (~4 ms, see 6) (?).
- Bug: on tick wrap (now < start), the code uses now + start instead of the modular difference. The open firmware should use unsigned subtraction.

### Channels
- Normal: the user channel, 0x20000052 (= param 0x16, settings+0x42).
- **Kbd Play** channel (param 0x17, settings+0x43; 'A' = same as user) is used only when all of these hold:
  - Seq mode;
  - playing;
  - not recording;
  - UI+0xf == 1 (Kbd Play).
- The Kbd Play channel applies to keys, aftertouch, mod strip, sustain CC and pitch-bend spring-back.

## 2. Velocity curves

- Selector: param 0x19 (settings+0x45), read in 0x08018314. Curve tables: 0 → kb+0x248, 1 → kb+0x24c, 2 → kb+0x250.
- Index names, from the vendor editor's device definition: 0 Lin, 1 Log, 2 AntiLog (aftertouch adds 3 Soft). The shapes agree: for the same travel time curve 1 gives a higher velocity than curve 0, and curve 2 a lower one.
- x = time between contacts in **100 µs ticks**.
- Each curve is piecewise linear with 6 segments (x0, y0, x1, y1, a, b, c). Evaluator: 0x08018b5a; per segment 0x08011858.
- For x0 < x ≤ x1: y = a·x / b − c (C integer division, truncating toward 0), clamped between y1 and y0.
- At or below the first x0, y = y0.

Breakpoints (x ticks → velocity):

| Curve | Points |
|---|---|
| 0 | 0→127, 10→127, 50→110, 250→40, 1000→10, 4000→1, 40000→1 |
| 1 | 0→127, 10→127, 60→110, 340→42, 1300→10, 4000→1, 40000→1 |
| 2 | 0→127, 10→127, 40→80, 100→34, 500→6, 4000→1, 40000→1 |

Segment coefficients a / b / c, in segment order:

| Curve | Coefficients |
|---|---|
| 0 | 0/1/−127 · −17/40/−131 · −10/29/−127 · −30/750/−50 · −3/1000/−13 · 0/1/−1 |
| 1 | 0/1/−127 · −17/50/−130 · −4/16/−124 · −32/960/−53 · −9/2700/−14 · 0/1/−1 |
| 2 | 0/1/−127 · −47/30/−142 · −2/3/−110 · −4/57/−41 · −5/3500/−7 · 0/1/−1 |

- Times of 1 ms or less give velocity 127. 400 ms or more gives velocity 1.
- Bug: x > 40000 (4 s) matches no segment. The raw tick count is returned and truncated to 8 bits, which gives a random velocity. The open firmware should clamp to 1.

## 3. Octave

- Object 0x200016C4 (pointer at 0x200000CC). Constructor 0x08018d8a(obj, ledPlus=3, ledMinus=4, limit=4). The signed value at +1 ranges −4..+4.
- up 0x08018d4c, down 0x08018d6a, reset 0x08018d3c, semitones 0x08018bee (= value·12).

Buttons (button_on_press 0x0800f3a0). Flags: Oct+ held is 0x2000005D, Oct− held is 0x2000005E, Shift is 0x2000005F.

| Press | Condition | Action |
|---|---|---|
| Oct− | Oct+ already held, no Shift | Octave reset to 0. |
| Oct− | otherwise, no Shift | Octave −1. |
| Oct+ | Oct− already held, no Shift | Octave reset. |
| Oct+ | otherwise, no Shift | Octave +1. |

- The change takes effect on the next note-on. Held notes keep their stored octave.

LEDs (0x08018c42):
- 0 → both Oct LEDs dark (the LED bits are active low, see hardware_map.md).
- |v| = 1/2/3/4 → the LED on that side blinks at period index 6/4/2/1, faster for larger shifts. The other LED is off.

Shift + Oct in **Arp** mode adjusts a second octave-like object, 0x200016D0 (pointer at 0x200000D0, limit 3). Its value is pushed to the arpeggiator (arp+0x14 through 0x0800d550): the arp octave range −3..+3, stepped once per pattern loop. See arp_chord.md.

Shift + Oct in **Seq** mode:
- Shift+Oct− selects Transpose mode (UI+0xf = 0); Shift+Oct+ selects Kbd Play mode (UI+0xf = 1). Handled in ui_button_event 0x080142f0, cases 2/3.
- Shift + both Oct buttons clears the current pattern (0x0801559e): all 64 steps become rest (0x82), velocity 100. It also sets flag 0x20000068 (?).

## 4. Transpose and Kbd Play (Seq mode)

- In Seq mode with UI+0xf == 0 (Transpose), a key press while the sequencer plays transposes the sequence instead of sounding a note. Details are in arp_chord.md and the seq notes.
- The latch flag is param 0x18 (settings+0x44), mirrored at 0x2000005C. It decides whether the transpose holds after key release (?).
- In Kbd Play mode the keys sound on the Kbd Play channel over the running sequence.

## 5. Shift + key (0x08018344)

| Keys (k = index, 0 = top) | Function | Values |
|---|---|---|
| k 0..10 (top 11 keys) | Seq swing → pattern +0x401 (0x0801577c) | k=10…0 → 50,53,55,57,59,61,64,67,70,73,75 % |
| k 11..15 | Seq gate → pattern +0x402 (0x08015476) | k=15…11 → 10,25,50,75,90 % |
| k 16..31 (bottom 16 keys) | MIDI channel 1..16 (ch = 31−k, so the bottom key is ch 1) | see below |

- The swing table is at 0x0801A0A2 and the gate table at 0x0801A0AD.
- Both setters also set the "dirty" bit 1 of pattern +0x403 (?). The pattern object is *0x200000F0, current pattern at +0x30.

Channel keys:
- **Shift only**:
  - Set the user channel (param 0x16).
  - If the channel changed and the seq is not playing, raise flag bit 0x100 at +0x80c of *0x20000104 (all-notes-off/flush request) (?).
  - Always reset the Kbd Play channel (param 0x17) to 'A' (follow user).
- **Shift + Oct+ held**: set the Kbd Play channel (param 0x17) to that key's channel. The user channel is unchanged.

## 6. Aftertouch (0x0800d1dc; sensor 0x2000123C)

Timing and input:
- Runs about every 2 ms: counter 0x200000D8 reloads to 4 and is decremented every 0.5 ms.
- Input is a 12-bit ADC. A rise is accepted immediately; a fall is accepted only if it is 32 counts or more (hysteresis).

Mapping:
- raw ≥ 2336 (0x920) → 127.
- raw ≤ 256 (0x100) → 0.
- Otherwise `table[(raw − 256)·255 / 2080]`, a 256-entry byte table.

| Param 0x1A (settings+0x46) | Table | Shape |
|---|---|---|
| 0 | 0x08019DA2 | Mildly convex |
| 1 | 0x08019EA2 | Linear |
| 2 | 0x08019FA2 | Strongly convex |
| 3 (hidden) | 0x08019DA2 | Range 0x180..0xC40 instead |

- Output slews by at most ±3 per update.
- Sent as channel pressure `0xD0|ch` on USB and DIN, only when the value changes and 0x200000D9 == 0.
- If the Mod-CV source setting (settings+0x7A) == 2, the Mod CV is driven to value·4095/127.
- The open firmware should copy the three 256-byte tables from flash.

## 7. Sustain pedal

Polarity detection runs once at boot (0x08011b04):
- ADC < 1000 → polarity 0.
- ADC > 3095 → polarity 1.
- In between, unchanged (?, default).

Runtime (0x08011ac4) uses a hysteresis window:
- ADC < 1000 → pressed = (polarity ≠ 0).
- ADC > 3095 → pressed = (polarity ≠ 1).
- In between → no change.

On an edge, ui_hold(pressed) (0x080141e4) is called. With Shift held, the pedal drives chord capture instead (see arp_chord.md).

## 8. Hold / sustain logic (object 0x20004710, pointer at 0x20000110)

Settings used:

| Setting | Param id | Offset |
|---|---|---|
| Pedal mode | 0x09 | +0x35 |
| Sustain channel | 0x0A | +0x36 |
| Sustain CC low value | 0x0B | +0x37 |
| Sustain CC high value | 0x0C | +0x38 |
| Sustain CC number | 0x0D | +0x39 |
| Hold-button mode | 0x0E | +0x3A |
| Hold-button function | 0x0F | +0x3B |
| Pedal function | 0x10 | +0x3C |

- **Function** is a bitmask:
  - bit 0 = Hold: sets or clears the hold flag at +6, which feeds the arp/seq (0x0800d660).
  - bit 1 = Sustain: sends one CC on activation (high value) and one on deactivation (low value).
  - So 1 = Hold, 2 = Sustain, 3 = both.
  - When the function is 2, the Hold LED is not touched.
- **Mode**: 0 = toggle (each press flips, release ignored); 1 = momentary/gate (on while pressed). Whether these match the MCC labels is (?).
- State fields: +4 pedal on, +5 button on, +6 hold flag, +8 CC already sent (prevents duplicates when pedal and button overlap).
- Handlers:
  - pedal press 0x08016456, pedal release 0x080164de;
  - button press 0x0801636c, button release 0x080163f4;
  - CC on 0x08016224, CC off 0x080162d0.
- Hold LED (0x0801925e): steady when hold is on. With chord mode on it blinks (pattern 5, or a 2/4/1 pattern via 0x08013ac8 when hold is also on).
- When the hold flag falls to 0, keyboard_process clears its latched-note list (kb+0xf4) and drops the gate.

## 9. Pitch strip (0x0800be40)

Reading:
- I2C, register 0x21, through an 8-step non-blocking state machine (0x0800ca82). One new sample every 8 main-loop passes.
- Raw 0 means no touch.

Scaling, using calibration cal[+6] (low) and cal[+7] (high):

    s = clamp(((raw − lo)·0x7F0 / (hi − lo)) >> 4, 0, 127)

Bend value v:

| s | v |
|---|---|
| 0..54 | s·64/54 |
| 55..73 | 64 (dead zone, 19 steps) |
| 74..127 | 64 + (s−74)·127/53 − (s−74)·64/53 (both truncating) |

MIDI and CV output:
- MIDI `0xE0|ch`, MSB = v, LSB = 127 if v == 127 else 0. So the resolution is 7-bit, and full up gives 0x3FFF.
- Channel is param 0x15 (settings+0x41) while touching.
- CV bend: 0x2000007C = v<<7 → 0x08016668.
- Sent only when v changes.

Release spring-back (0x0800c9d0):
1. Re-send the last value.
2. Step toward 64 through table 0x08019D91 = [0,2,4,5,7,8,10,13,15,17,20,24,28,33,39,48,64], one step per new strip sample.
   - Below centre the start index is (64−v)>>2.
   - Above centre the start index is (v>>2) − 16, mirrored.
- During spring-back the channel can be the Kbd Play channel; during touch it never is. This inconsistency is in the stock firmware.

## 10. Mod strip (0x0800c0f0)

- Same scaling as the pitch strip, giving s = 0..127. No dead zone.
- Sends CC number param 0x14 (settings+0x40, default 1) with value s.
  - Only while touched, and only when the value changes.
  - No return to 0 on release.
- Channel is param 0x13 (settings+0x3F), or the Kbd Play channel (see 1).
- When the Mod-CV source setting (settings+0x7A) == 0, the Mod CV follows the strip.

## 11. Factory test mode

When settings+0xA9 is set, the strips and pedal report through 0x0801107c / 0x080110e2 instead of sending MIDI.

## Open questions
- Real main-loop (scan) rate. Measure on hardware.
- Hold/pedal mode value 0/1 vs the MCC "Gate/Toggle" labels.
- Purpose of flag 0x20000068 and bit 0x100 at +0x80c.
