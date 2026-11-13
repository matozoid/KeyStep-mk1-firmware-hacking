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

- **Web MIDI editor.** A single HTML page talking SysEx: piano-roll editing of the 8 sequences, all
  settings (including ones MCC doesn't expose), single-file backup/restore of everything, `.mid`
  import/export. Include a simulated KeyStep so the page works without hardware.
- **Browser flasher.** The bootloader protocol is decoded ([bootloader_protocol.md](bootloader_protocol.md)),
  so the page could flash the firmware over Web MIDI without MIDI Control Center.
- **Playable manual.** A clickable virtual KeyStep with help on each control and Shift combination.
  Shift-combo features on a screenless device are hard to discover otherwise.
- **Stock bug catalogue.** KSCON documents stock firmware quirks and their workarounds. A
  `re/stock_bugs.md` for the Mk1 would give the replacement firmware a checklist of things to fix.
- **Whole-image emulation tests.** KSCON runs its firmware code in a Unicorn emulator with stubbed
  hardware calls. Complements the existing host unit tests (`Firmware/tests`) by exercising the real
  ARM build: boot, main loop, sequencer/arp timing.
