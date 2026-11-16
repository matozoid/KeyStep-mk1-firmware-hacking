# Stock feature set (baseline)

What the stock KeyStep firmware does, summarised from the user manual (March 2016) and checked
against the code where noted. This is the baseline the replacement firmware should match or beat.
Hardware details are in [hardware_map.md](hardware_map.md).

## Keyboard and controls

- 32 keys, velocity (3 curves: Log / Lin / AntiLog) and channel aftertouch (MCC offers Lin / Log / AntiLog / Soft, see [settings.md](settings.md)).
- Oct− / Oct+: ±4 octaves, buttons flash faster the further from centre; both together = centre.
  Shift takes effect on the next note played.
- Pitch strip: springs back to centre, dead zone in the middle. Mod strip: stays where released,
  assignable CC (default 1). Each has its own MIDI channel setting (fixed or follows keyboard).
- Sustain pedal: polarity sensed at power-up. Mode momentary / latching; function Hold, Sustain or
  both; CC number and min/max values assignable.
- Hold button: same options as the pedal (momentary/latching, Hold/Sustain/both).
- Rate knob 30–240 BPM, Jump or Pick-up mode. Tap tempo averages 2, 3 or 4 taps.

## Shift combinations

| Combination | Effect |
|---|---|
| Shift + key 1–16 | Keyboard MIDI channel (also resets the Kbd Play channel) |
| Shift + key 17–21 | Seq/Arp gate length, 10–90 % |
| Shift + key 22–32 | Swing, Off (50 %) to 75 %, 11 steps |
| Shift + Oct− / Oct+ | Transpose mode / Kbd Play mode (mutually exclusive) |
| Shift + Oct+ + key | Kbd Play MIDI channel |
| Shift + Hold | Chord mode on/off; hold both and play up to 16 notes to set the chord |
| Shift + Rec | Record append |
| Rec held + keys 1–16 | Set sequence length; presses add up, maximum 64. Also the loop length of the Pattern random arp |
| Shift + Stop | Clear last step (Seq mode) |
| Shift + Play | Restart Seq/Arp from the first step |
| Shift + Mode/Time Div knob | New position applies when Shift is released |
| Stop × 3 | All Notes Off (code: CC 123 on all 16 channels) |
| Oct− + Oct+ at power-up | Factory reset |
| Rec + Stop + Play at power-up | Bootloader / update mode |

## Sequencer

- 8 sequences × up to 64 steps, up to 8 notes per step; stored in flash.
- Step recording with rests and ties (Tap button), legato; real-time record/replace.
- Per-sequence gate and swing; arp has its own gate and swing.
- Records note and velocity (or a fixed velocity), not controllers.
- Next Seq: at end / instant reset / instant continue.
- Transpose from the keys, or from external MIDI (USB, DIN or both; channel; latch; centre note,
  default 60).
- Play/Pause button can be set to "Reset" instead of pause.

## Arpeggiator

- Modes: Up, Down, Inclusive, Exclusive, Random, Order, Up×2, Down×2.
- Hold latches the arpeggio; adding notes while one key stays down builds up to 32 notes.
- Works together with Chord mode.

## Sync

- Rear switches: Internal, USB, MIDI, Sync In. Seq/Arp won't run on an external source with no clock.
- As master: MIDI clock on DIN and USB, pulses on Sync Out.
- As slave: passes the clock to all outputs, converts any type to MIDI clock; falls back to the last
  tempo when the external clock stops.
- Sync In/Out types: 1 step (Gate), 1 step (Clock, default), 1 pulse (Korg), 24 PPQ, 48 PPQ.
  Start: on clock, or on clock plus TRS ring high.

## CV / Gate / Mod

- Pitch: V/oct (0–10 V, 0 V note selectable, default C-2) or Hz/V (1 V note selectable, default C0).
  Bend range 1–24 semitones. Note priority Low / High / Last.
- Gate: S-trig, V-trig 5 V, V-trig 12 V.
- Mod: source mod strip, velocity or aftertouch; max 5–12 V in 1 V steps.
- MIDI-to-CV from USB or DIN on a selectable input channel while Seq/Arp are stopped.

## MIDI

- USB MIDI and DIN in/out; USB-to-DIN interface; MIDI Thru option.
- Transport buttons send MMC, CC or both; channel and CC number per button.
