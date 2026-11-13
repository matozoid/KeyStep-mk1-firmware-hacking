# Ideas for the new firmware

Improvements over the stock feature set ([re/stock_features.md](re/stock_features.md)).

## Sequencer

- **16 tracks per sequence, one per MIDI channel.** Same capabilities as the stock sequencer
  (64 steps, up to 8 notes per step, gate, swing, ties, rests, step and real-time recording), but each
  of the 8 sequences holds a track for every MIDI channel. The keyboard MIDI channel (Shift + key 1–16)
  selects the track to record into; all tracks play together.
  - Storage: stock uses 1 KB per sequence, so a 16-track sequence is 16 KB. Flash: the stock app is
    ~94 KB and leaves ~126 KB unused, so a few 16-track sequences fit easily; all 8 (128 KB) is
    borderline — use sparse storage or make only some sequences multi-track.
  - RAM (48 KB) is the tighter limit: play directly from memory-mapped flash and keep only the
    sequence being recorded/edited in RAM (≤ 16 KB).
  - Open: which track drives the CV/Gate outputs (selectable, or follows the keyboard channel);
    per-track mute; per-track length, gate and swing or shared.

## Generative modes

From [yonie/keystep-mods](https://github.com/yonie/keystep-mods) (KeyStep Pro firmware patches),
adapted to the Mk1.

- **Flux: evolving random loop** (Turing-machine style). A short random note loop that repeats and
  mutates slightly on each pass. Parameters: pitch range (snapped to a scale), loop length,
  density (% of steps that play), change probability (0 = locked loop), and a "new riff" action.
- **Pulse: Euclidean rhythms.** Lanes that spread N pulses evenly over M steps, each on its own note
  (e.g. kick 36 / snare 38 / hat 42), with rotation, per-pulse chance and velocity variation.
- Shared: runs in place of a sequence without overwriting it; Record captures what it plays into
  the sequence so a good loop can be kept; follows tempo, swing, transport and the MIDI channel.
- Open for the Mk1: no screen and few controls. Parameters could live on Shift + keys (like gate
  and swing) and the Mode / Time Div knobs; entry via e.g. Shift + Seq/Arp switch or a Mode knob
  position. Fits naturally with the 16-track idea (one generator per channel).

## MIDI

- **Skip redundant program changes.** If sequences get per-track Bank Select / Program Change, only
  send them when the values differ from what was last sent on that channel since power-on (first
  send always goes out). Avoids synths reloading a patch, and playing the first note late, on every
  sequence switch. (Also from yonie/keystep-mods.)

## Playing

From [canadianblaken/keystep-con](https://github.com/canadianblaken/keystep-con) (KSCON, KeyStep Pro
firmware patch), adapted to the Mk1.

- **Step play.** Each key press plays the next step of the current sequence (its pitches or chord), so
  a stored line can be played in your own rhythm with one finger. Reuses the sequence data as is.
- **Root chords / scale chords.** Stock chord mode needs a chord set up by holding notes. Instead: one
  key sets the root and other keys pick a chord type (maj, min, 7, sus, …); or a diatonic variant where
  a scale is chosen and each key plays its scale chord. Works with the arpeggiator, so held chords
  can be arpeggiated.
- **Bass generator.** Turns chords into a bass line, with styles Root, Root + 5th, Octaves, Pulse and
  Follow rhythm. On the Mk1 this could be an extra arp mode, or a track that follows chords coming in
  on the MIDI transpose input. Natural fit for the CV output driving a mono bass synth.
- **Mod strip lock.** Shift + touch the mod strip freezes it: no CC or mod CV is sent until toggled
  again.
- **Drum lanes.** With 16 tracks per sequence, keys 1–16 can act as drum lanes (GM notes on channel 10)
  while another channel is played from the keys. Overlaps with Pulse (Euclidean rhythms).

## Tooling

Also from KSCON, which ships a single-page web app alongside its firmware patch. Even more useful on
the Mk1, which has no screen.

- **New MIDI Control Center.** A replacement for Arturia's MCC that only supports the KeyStep Mk1 and
  also runs on Linux. Candidate stack: Kotlin Multiplatform with Compose Multiplatform, targeting
  Linux, Windows, macOS (JVM desktop) and the web (Kotlin/Wasm or JS on Web MIDI). MIDI access per
  target: `javax.sound.midi` on desktop (macOS SysEx needs CoreMIDI4J or a native bridge), Web MIDI
  with SysEx permission in Chrome/Edge.
  - Features: all global settings (including the ones MCC hides, see
    [re/settings.md](re/settings.md) §2a), piano-roll editing of the 8 sequences, single-file
    backup/restore, `.mid` import/export, a MIDI monitor, and the firmware flasher below.
  - Holds settings for the new firmware's features, so rarely changed options (scales, chord presets,
    generator parameters, clock divider, per-track channels) live in the app instead of on yet more
    Shift + key combinations. The device keeps combinations only for things changed while playing.
  - Speaks the stock SysEx protocol ([re/sysex.md](re/sysex.md)) so it also works with stock
    firmware; new settings use the same protocol with new parameter codes.
  - Include a simulated KeyStep so it works without hardware.
- **Firmware flasher.** Part of the app above, on every target including the browser. The bootloader
  protocol is decoded ([bootloader_protocol.md](bootloader_protocol.md)), so no Arturia software is
  needed to install or restore firmware.
- **Playable manual.** A clickable virtual KeyStep with help on each control and Shift combination.
  Shift-combo features on a screenless device are hard to discover otherwise.
- **Stock bug catalogue.** KSCON documents stock firmware quirks and their workarounds. A
  `re/stock_bugs.md` for the Mk1 would give the replacement firmware a checklist of things to fix.
- **Whole-image emulation tests.** KSCON runs its firmware code in a Unicorn emulator with stubbed
  hardware calls. Complements the existing host unit tests (`Firmware/tests`) by exercising the real
  ARM build: boot, main loop, sequencer/arp timing.

## From the KeyStep Pro

Stock KeyStep Pro features that fit the Mk1 hardware (from memory of the Pro, not checked against its
manual). With no screen, each needs a Shift combination or the web editor.

### Sequencer

- **Scale quantize.** Pick a scale and root; recorded notes, transpose and generators snap to it. Shared
  by scale chords, Flux and the bass generator.
- **Playback direction.** Forward, reverse, ping-pong, random. Fits on Shift + keys.
- **Probability and randomness.** Per-step chance of playing, plus small random variation of velocity,
  gate and pitch. A light version of Flux applied to a stored sequence.
- **Length and time division per sequence/track.** With 16 tracks, different lengths and divisions give
  polyrhythms. Answers the per-track vs shared question under the 16-track idea.
- **Time shift.** Nudge the whole sequence earlier or later by a step or tick.
- **Mute / solo per track.** Needed once there are 16 tracks; e.g. Shift + Oct + key 1–16.

### Arrangement

- **Chains.** Play sequences in a set order (e.g. 1 → 1 → 2 → 3) as a simple song mode. Stock only
  switches to a chosen next sequence.
- **Scenes.** One action recalls a combination of sequences across tracks. Only useful with 16 tracks.

### Recording

- **Overdub with undo.** Real-time recording that layers onto the sequence, with undo of the last take.
  Stock only has record/replace and append.
- **Per-track transpose.** Stock transposes the whole sequence; with 16 tracks, transpose only the
  selected track (or exclude drum tracks).

### Arpeggiator

- **More modes.** E.g. a pedal-tone mode alternating a fixed note with the arp notes. (Stock already
  has an octave range of ±3 via Shift + Oct in arp mode, see [re/arp_chord.md](re/arp_chord.md).)

### Sync

- **Clock divider and reset on Sync Out.** Divide or multiply the output clock (e.g. ×2, /2, /4)
  independently of the internal step rate, and send a reset pulse on Play. Useful for Eurorack. Stock
  only offers fixed pulse types (1 step, Korg pulse, 24/48 PPQ).

## From the KeyStep mk2 / 37 mk2

Checked against MCC 1.23's device definitions (`KeyStep2.json`, `KeyStep2_37.json`). The original
KeyStep 37 (`KeyStep37.json`) has only the Mk1 settings plus four CC knobs, so scale, chord and Spice
features come from the mk2 generation.

- **Scales.** A global scale and root, overridable per pattern ("Pattern Scale": Off / Global / scale).
  Scales: Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor, Blues,
  Pentatonic Major/Minor, Japanese, Gypsy, Arabic, Freygish, User. Covers keyboard scale mode and scale
  quantize (KeyStep Pro section); the list is a ready-made table to copy.
- **Chord presets per pattern.** Oct, 5th, Maj, min, sus2, sus4, Maj7, min7, Maj9, min9, Maj11, min11,
  or a user chord of up to 16 notes stored as signed semitone offsets ("UCO", default 0 4 7). Matches
  root / scale chords under Playing.
- **Strum.** Chord notes spread in time, in ms (0–500) or synced (1/64 … 1/4), with type Up, Alt Up,
  Down, Alt Down, Random.
- **Voicing and spread.** Voicing Off / On / Bass −1 / −2 / −3 (drop the bass note octaves); Spread
  from velocity, aftertouch or a fixed 1–16.
- **Spice & Dice.** Per-sequence randomisation amount ("Spice", 0–100 % in MCC). Dice (a button, not
  in MCC) rolls a new variation of velocity, gate, octave and step on/off; the stored sequence stays
  untouched and a roll repeats until the next Dice. Differs from per-step probability (KeyStep Pro
  section) in that each roll is fixed and repeatable. Mk1 controls: e.g. Shift + Rate knob, Shift + Tap.
- **Recording options.** Count-in, record quantise, record automation (controllers into the sequence).
- **Per-pattern program change.** Bank MSB/LSB and program sent on pattern change, with a global
  on/off. Fits "Skip redundant program changes" under MIDI.
- **Per-pattern tempo.** "Use Patt. Tempo": Off / On / When Paused.
- **Pattern switch timing.** "Wait to Load": At End / Instant / 1 Bar. Stock Mk1 has End / Instant
  Reset / Instant Continue; add 1 Bar.
- **Gate retrigger.** CV gate Mono / Legato / Legato Slide.
- **Wheel vibrato.** Mod strip adds vibrato (depth 0–120, rate 3–100) instead of, or as well as,
  sending CC.
- **More mod CV sources.** Random and Pulse besides velocity, aftertouch and mod wheel.
- **Velocity curves with fixed values.** Fixed 32 / 64 / 100 / 127 next to Lin / Log / Exp.
- **Aftertouch sensitivity.** Low / Medium / High, on top of the curve.
- **Expression pedal.** Pedal input as Sustain or Expression (continuous CC). Needs a hardware check:
  the Mk1 pedal input may only read on/off.
- **Sync types.** Separate Sync In / Sync Out types, including 4 PPQ (Sync Out: 1, 4, 24, 48 PPQ,
  Korg). Overlaps the clock divider idea.
- **Separate clock / transport send and receive switches**, and "Auto" clock source selection.
- **LED intensity presets.** Dim / Normal / Night (stock Mk1 has 10–100 %).
