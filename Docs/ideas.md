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
