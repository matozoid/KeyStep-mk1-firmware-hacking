# First hardware check

Facts that only a real KeyStep Mk1 can confirm. Work through them in order and note the results
in the docs (`Docs/re/`) and in the code where marked.

The stock bootloader is never touched, so the official firmware can always be put back the same
way.

## 0. Flash

`../Tools/TestRig/keystep_testrig.sh --flash build/keystep.bin` packs the image, enters the updater
and uploads. With no firmware running, hold Rec + Stop + Play while plugging in USB.

## Results so far (2026-11-16)

Confirmed on a real unit:

- Boot: light show, then Stop lit and Tap blinking. No watchdog resets (the bootloader does not
  start the IWDG).
- USB: enumerates as `1209:0001 KeyStep Open`; the identity reply is byte-identical to stock.
- Key order: 41 (leftmost) to 72 (rightmost) at octave 0, all 32 keys.
- Velocity: soft 17–60, hard 98–108 on repeated strokes, up to 120. Hard strokes take 1.5–4 ms
  of contact travel, which is why the keybed is scanned on every main-loop pass.
- Aftertouch 0–127; pitch strip −8192..+8191 with return to 0; mod strip CC 1, 0–127.
- Transport: CC 54 + Start, CC 51 + Stop, CC 50 for Record, values 127/0, one event per press.
- Rate knob 30–240 BPM; Seq/Arp switch; arp with a held chord; step record and playback.
- Sequences stored by the stock firmware play with the same notes, velocities and swing.
- Recorded steps survive a power cycle.
- Step record with Tap (Tie to Previous): rest, a held key with two taps giving one note over two
  steps, and legato entry with Tap held. Tie to Next is covered by the host tests only.
- Updater entry over SysEx (flag + reset) and with the button combo.

Known and open:

- A flash save used to stop the main loop for about 50 ms per page (MIDI clock gap of 69 ms
  after Stop). Saves now run in the background: the gap at Stop is 25 ms, within the timing noise
  of the host log, and the worst gap seen is 43 ms. A page erase still stops the CPU for 20-40 ms;
  that cannot be avoided on this chip.
- Flipping the Seq/Arp switch while playing sends Stop and Start 2 ms apart. Compare with stock.
- Not exercised yet: sections 3 to 5 below, pedal, DIN MIDI, sync, MCC.

## 1. First boot

- [x] LEDs light one at a time: Play, Stop, Rec, Tap, Hold, Oct−, Oct+, Shift. Then all glow
      briefly together and go off.
- [x] Afterwards Stop is lit and the unit enumerates as "KeyStep Open" (or "Arturia KeyStep 32"
      with `STOCK_USB_ID=1`).

## 2. Key order (same indexing as stock; this confirms the physical layout)

Open a MIDI monitor with octave 0 and play every key from left to right.

- [x] Each key gives a note one semitone above the key to its left, and the rightmost key gives
      note 72.
- If not, note the note each key sends. The fix goes in the key-to-note mapping (`app_key_base_note`
      / `KEY_TOP_NOTE`) or in the row order in `src/core/keyscan.c`.

## 3. Test mode (sysex.md 5)

Send these SysEx messages from any SysEx tool, with a MIDI monitor on the USB port.

| What | Message |
|---|---|
| Test mode on | `F0 00 20 6B 7F 42 02 7D 7D 09 7F F7` |
| Test mode off | `F0 00 20 6B 7F 42 02 7D 7D 09 00 F7` |
| Mod CV, value v | `F0 00 20 6B 7F 42 02 00 73 11 v F7` |
| Pitch CV, note n | `F0 00 20 6B 7F 42 02 00 73 10 n F7` |
| Gate on 5 V / off | `F0 00 20 6B 7F 42 02 00 73 12 01 F7` / `... 12 00 F7` |

With test mode on:

- [ ] **Strip full scale:** slide each strip from end to end. CC 0x41 (pitch) and CC 0x40 (mod)
      should cover 0..127. If they don't, recalibrate with MCC and record the raw end points.
- [ ] **PD4 = DC jack:** plug the DC adapter in and out. CC 0x60 should go 1 / 0 and the LEDs should
      fade between brighter and dimmer. If CC 0x60 doesn't follow the adapter, fix the PD4 row in
      `Docs/re/hardware_map.md`.
- [ ] Knobs (CC 0x66, 0x15, 0x68), switches (CC 0x12, 0x13), pedal (CC 0x42) and buttons (control
      IDs, value 1) all report.

## 4. CV measurements (multimeter)

- [ ] **Mod CV full scale:** v = 0x60 should read 10.0 V (the factory test value). Also measure
      v = 0x7E. Record both in `Docs/re/hardware_map.md`. If 0x60 isn't 10 V, adjust `mod_code`
      in `src/core/app.c`.
- [ ] **Pitch CV:** n = 36 and n = 48 should differ by 1.000 V (V/oct). Recalibrate in MCC if the
      offset is wrong.
- [ ] **Gate:** gate on with value 01 gives about 5 V, value 02 about 12 V.

## 5. Loopbacks (cables)

- [ ] DIN Out → DIN In, send `F0 00 20 6B 7F 42 02 00 70 00 00 F7`: expect CC `B0 02 70`.
- [ ] Sync Out → Sync In, send `... 02 00 71 00 00 F7` and `... 02 00 72 00 00 F7`: expect
      `B0 02 71` and `B0 02 72`.

## 6. Normal use

Leave test mode, then check each of these against the stock firmware:

- [ ] keys, velocity and aftertouch
- [ ] arp and sequencer record / play
- [ ] sync in and out with external gear
- [ ] MCC: read and write the settings, and transfer sequences

Settings and sequences must survive a power cycle.
