# MIDI routing (stock firmware 1.1.0.28)

How the stock firmware moves MIDI between the keyboard, the sequencer/arp, USB, the 5-pin DIN port and the CV outputs. Addresses refer to the stock image. Settings offsets are relative to the settings object at `0x20005058`. A setting *code* `n` is stored at `settings+0x2C+n`. Unconfirmed inferences are marked (?).

Port numbering used throughout the input code: **0 = USB**, **1 = DIN**.

SysTick runs at 10 kHz, so one HAL tick is 100 µs. The "100 ms" timers mentioned below are counters that the SysTick handler decrements once every 1000 ticks.

## 1. Output side

### 1.1 Router object

| Item | Address | Notes |
|---|---|---|
| Router singleton | `0x20000258` | Created on first use by `midi_router_get` `0x08019b0c` (ctor `0x08016a38`, 0xA0 bytes) |
| Sysex / universal queue | `0x2000025C` | ctor `0x08016ed8` (0x284 bytes), serviced by `0x08016a90` |
| `midi_out_usb` | `0x080169c4` | Pushes one packed 3-byte message into a 36-word ring (head at [0], tail at [1]). If the ring is full the message is dropped silently. |
| `midi_out_din` | `0x080166c8` | Works out the message length, then calls `din_tx_write` `0x0800cd38` |
| Local send helper | `0x0801699c` | DIN first, then USB, then the CV handler |

### 1.2 DIN transmit

`din_tx_write` copies the message into a 256-byte ring at `*0x20000078`. It does this only if the **whole** message fits; otherwise it drops the whole message, so there is never a partial write. It then enables the USART1 TXE interrupt, and `USART1_IRQHandler` `0x0800a440` sends the ring one byte at a time. When the ring is empty, the handler turns TXE off again.

Message length used by `midi_out_din`:

| Status | Bytes |
|---|---|
| 8x 9x Ax Bx Ex, F2 | 3 |
| Cx Dx, F1, F3 | 2 |
| everything else (realtime, F6 …) | 1 |

The TX interrupt also has a path where a flag at `0x20000064` sends `0xF8` ahead of everything in the ring. Nothing in the image appears to set this flag (the only literal reference is inside the IRQ handler). It looks like an unused "priority clock" feature (?).

### 1.3 USB transmit

The main loop calls `midi_out_service` `0x0801674c`. Once more than 4 HAL ticks (≈500 µs) have passed since the last transfer, it does the following:

1. It gives the sysex queue (`0x2000025C`) the first chance to send.
2. Only if that queue sent nothing does it drain the short-message ring (`0x08016870`).

Each short message becomes one USB-MIDI event (`usb_midi_pack` `0x080167e8`). All events use cable 0.

| Status | CIN |
|---|---|
| 8x–Ex | status >> 4 |
| F1, F3 | 2 |
| F2 | 3 |
| F0, F4–FF | 0xF |

The packets go to bulk endpoint `0x81` from buffer `0x2000519A`. Sysex sent from the queue is packed with CIN 4/5/6/7 in the usual way.

### 1.4 What goes where

| Source | USB | DIN | Condition |
|---|---|---|---|
| Keys, strips, aftertouch, mod/pitch | yes | yes | always |
| Arp / sequencer notes | yes | yes | always |
| Transport CCs (§4) | yes | yes | transport mode has the CC bit |
| MMC sysex (§4) | yes, unless sync source is USB | yes | transport mode has the MMC bit |
| Clock F8 out | yes | yes | sync source is Internal or Sync In |
| FA / FB / FC out | yes | yes | sync source is Internal or Sync In |
| All-notes-off (Stop×3) | yes | yes | always |
| Sysex replies (identity, get, dumps) | yes | no | always |
| Incoming USB short messages | – | yes | MIDI Thru on |
| Incoming DIN short messages | no | yes (echo) | MIDI Thru on |
| Incoming realtime when synced (§5) | see §5 | see §5 | |
| Incoming sysex | never forwarded | never forwarded | |

The firmware has no separate "USB↔MIDI interface" mode. The only way incoming USB data reaches the DIN output is the generic **MIDI Thru** path (§2.4). Incoming DIN data never goes out over USB, except for the realtime bytes that the clock code forwards (§5) (?).

## 2. Input side

### 2.1 MIDI-in object

The MIDI-in object lives at `0x2000471C`. Its ctor is `0x08012c40`, its vtable is `0x0801AC18`, and the global pointer `*0x20000114` points to it. The init function `0x08012918` stores pointers to settings, the clock and so on at `+0x8d4..+0x8f8`.

| Offset | What |
|---|---|
| +0x58 | Parser for port 0 (USB) |
| +0x140 | 256-byte USB raw-byte ring |
| +0x248 | Parser for port 1 (DIN) |
| +0x330 | DIN realtime/message ring, 32 entries |
| +0x3b8 | CC tracker (pitch bend, pressure, CC1, RPN) |
| +0x3c4 | 128-note table used for MIDI-to-CV |
| +0x64c | 128-note table used for external transpose |

### 2.2 DIN receive

The USART1 RXNE interrupt calls `midi_din_rx_byte` `0x08012906`. That function feeds the byte straight into the DIN parser, inside the interrupt. No receive buffer is used before the parser.

### 2.3 USB receive

- The OUT-endpoint callback `0x080198dc` calls `usb_midi_rx_packets` `0x08012740`.
- If fewer than 0x50 bytes are free in the ring at +0x140, the whole transfer is rejected.
- Otherwise, each 4-byte event is unpacked using a length table at `0x200001D2`. The table is indexed by the low nibble of byte 0, i.e. the CIN. **The cable number is ignored**, so all 16 virtual cables are merged.
- The main loop calls `midi_usb_parse_pending` `0x080128ae` twice per loop. Each call feeds up to 50 bytes from the ring into the USB parser.

### 2.4 Byte parser `0x0800f9f8`

There is one instance per port; its state byte is at +0xe0.

| Input | Behaviour |
|---|---|
| 8x–Ex | Sets running status. Expects 1 data byte for Cx/Dx and 2 for the others. After each complete message the parser returns to the data-expect state, so running status works. |
| F0 | Enters the sysex state. Data bytes go to the sysex decoder at +0x28 (`0x080107f8`). |
| Any status byte during sysex | Ends the sysex. The end handler `0x08010dc2` is told the sysex is "complete" only when the byte is F7; any other status byte aborts it. |
| F1, F3 | Wait for 1 data byte. F2 waits for 2. Quirk: they behave like running status, so following data bytes repeat the message (?) |
| F4, F5, F6 | Ignored. Running status is kept. |
| F7 outside sysex | Resets the state to idle |
| F8–FF | Dispatched at once. The parser state is not touched, so realtime bytes can appear in the middle of a message. |

Complete messages are passed to the listeners through `midi_dispatch_listeners` `0x08019ad2`, together with the port number.

### 2.5 Short-message routing — `midi_in_on_message` `0x08012b28`

**USB (port 0)**
1. If the sync source is USB, the realtime/SPP handler (§5) runs straight away.
2. The message is fed to the MIDI-to-CV note table (+0x3c4).
3. If the transpose-port setting has bit 0 set, the message is also fed to the transpose table (+0x64c).

**DIN (port 1)**
1. The message is pushed into the ring at +0x330. The main loop drains it with `midi_din_rt_drain` `0x0801284c`, at most 20 messages per loop. Each message goes through `0x080127f4`, which calls the realtime handler only if the sync source is MIDI.
2. The message is fed to the MIDI-to-CV note table.
3. If the transpose-port setting has bit 1 set, the message is also fed to the transpose table.

**Both ports**
- **CC tracker** `0x08011bf2`. It only looks at the MIDI-in channel and records:
  - pitch bend (Ex), as a 14-bit value;
  - channel pressure (Dx);
  - CC1;
  - RPN selection (CC 101/100; CC 99/98 clears it);
  - CC6 data entry while RPN 0,0 is selected, which sets the pitch-bend range.
- **Thru.** If MIDI Thru is on, the short message is copied to the DIN output. The exception is FA/FB/FC/F8 while the sync source is Internal; those are not forwarded, so the KeyStep's own clock is not mixed with a foreign one.

Relevant settings:

| Setting | Code | Offset | Values |
|---|---|---|---|
| MIDI Thru | 0x08 | +0x34 | 0 off, non-zero on |
| MIDI-in (MIDI-to-CV) channel | 0x07 | +0x33 | 0–15, `'A'` (0x41) = follow user channel |
| User channel | 0x16 | +0x42 | 0–15 |
| Transpose input channel | 0x21 | +0x4d | as above |
| Transpose input port | 0x27 | +0x53 | 1 USB, 2 DIN, 3 both (bit mask) |
| Transpose latch | 0x26 | +0x52 | 0 = transpose resets when the key is released |
| Transpose centre note | 0x2B | +0x57 | default 60 |
| Pitch-bend range | 0x50 | +0x7c | 1–24 |
| Mod source | 0x4E | +0x7a | 0 = CC1, 2 = aftertouch (?) |
| Note priority (keyboard) | 0x4D | +0x79 | 0 low, 1 high, 2 last |

`'A'` channels are resolved by `settings_get_code` `0x080192c4`. Whenever one of codes 0x07, 0x16 or 0x21 changes, the settings listener `0x08012980` updates the channel cached in each note table (`0x080127b8`).

### 2.6 Sysex receive

Sysex is handled **only for USB**. `midi_in_on_sysex` `0x08012a88` returns straight away for DIN. See §7.

## 3. MIDI-to-CV — `midi_to_cv_process` `0x08012ee4`

The main loop calls this function.

- **Note table** (`note_table_feed` `0x08013fde`, 128 entries):
  - On a note-on, the entry stores a counter that only ever increases, which records the arrival order. The velocity is stored at +0x208.
  - A note-off is 8x, or 9x with velocity 0.
  - The table's channel byte is at +1; the value 0x7E means omni.
- **Priority**: `note_table_pick128` `0x080191ec` chooses the note according to mode byte 0 (0 lowest, 1 highest, 2 last). The ctor (`0x08014052`) sets the MIDI table to mode 2 and nothing changes it later. So **MIDI-to-CV is always last-note priority**. The Note Priority setting (+0x79) changes only the two keyboard tables (+0xf4 / +0x19c) (?).
- **When it is active**: only while the sequencer/arp is not playing (`ui+0xd != 2`).
  - On the change to playing, the held MIDI note is released.
  - While playing, the CV/gate outputs belong to the seq/arp.
  - The chosen note is stored at `0x20000054` (`0x080190c4`) and sent to the CV note path, with the pitch bend from the CC tracker added.
- **Mod CV**:
  - With mod source 0, incoming CC1 drives the Mod CV.
  - With mod source 2, incoming channel pressure drives it.
  - The scaling is value × 4095 / 127.
- **Pitch-bend range** can be set remotely via RPN 0 (CC101=0, CC100=0, CC6=n). The value is stored in setting 0x50 and clamped to 1–24.

## 4. Transport buttons (MMC / CC)

Transport mode is the byte at `settings+0x8c`. Bit 0 = send CCs, bit 1 = send MMC, so 1 = CC, 2 = MMC, 3 = both.

### 4.1 CC mode

| Button | Channel (code / offset) | CC number offset | Press | Release |
|---|---|---|---|---|
| Stop | 0x61 / +0x8d | +0x90 | 127 | 0 |
| Play | 0x63 / +0x8f | +0x92 | 127 | 0 |
| Record | 0x62 / +0x8e | +0x91 | 127 when record turns on | 0 when record turns off |

The record CC follows the record *state* (`ui_poll_record_cc` `0x08014dec`), not the button. The channel codes accept `'A'` (follow the user channel). Messages go to USB and DIN.

### 4.2 MMC mode

The frame is `F0 7F 7F 06 <cmd> F7`. It is built in the queue at `0x2000025C` (`0x08011d8a` / `0x08016d38`). The device ID comes from byte 0 of the queue object, which is fixed at 0x7F.

| Event | cmd |
|---|---|
| Stop | 01 |
| Play | 02 |
| Pause | 09 |
| Record on (strobe) | 06 |
| Record off (exit) | 07 |

The frame is always sent to DIN. It goes to USB only if the sync source is not USB, so the DAW does not get its own transport echoed back. The queue's port mask (`0x08016e68`) uses bit 0 for USB and bit 1 for DIN.

### 4.3 Stop × 3 = panic

The panic logic is in the button-press handler for button id 6 (Stop).

1. The first press arms a counter and starts a window: `0x2000008C = 6`, which the 100 ms timer counts down, so about 600 ms. Later presses do **not** extend the window.
2. A third press inside the window triggers the panic:
   - CC 123, value 0, is sent on all 16 channels to USB and DIN.
   - The gate is set low.
   - The sync-out lines are reset. This is skipped when Arm to Start (`settings+0x59`) is on and the master runs (`ui+0x13 == 2`).

## 5. Clock and transport sync

The sync source is returned by `clock_get_source` `0x0800e5c8` on the clock object `*0x200000EC`:

| Value | Source |
|---|---|
| 0 | Internal |
| 1 | USB |
| 2 | Sync In (analog) |
| 3 | MIDI DIN |

The source follows the rear sync switch, which is mapped in `0x08018a10`. `clock_set_source` `0x0800e5d0` applies the analog clock type only for source 2.

### 5.1 Clock out

- The tick routine `0x080101ec` is called from the clock engine `0x0800e438`. It sends **F8 to DIN and USB** only if the source is Internal or Sync In. It also blinks the tempo LED every 12 ticks (24 PPQN).
- `clock_transport` `0x0800e01c` takes these commands:

| cmd | Meaning | Byte sent |
|---|---|---|
| 0 | stop | FC |
| 1 | start | FA |
| 2 | pause | FC |
| 3 | continue | FB |
| 4 | Sync-In specific (?) | – |

  These bytes are sent only for source Internal or Sync In. The same function also sends MMC when the MMC bit is set (§4.2).
- No outgoing Song Position Pointer was found (?).

When synced to USB or MIDI, the KeyStep does not create its own clock bytes; it only forwards the incoming ones (below).

### 5.2 Clock in — `midi_clock_rt_handler` `0x0800ce68`

This handler runs only for the port that matches the sync source. For USB it runs straight away from the message handler; for DIN it runs from the ring in the main loop.

| Byte | Action |
|---|---|
| F2 SPP | The 14-bit position goes to the clock (`0x0800e6dc`). Position 0 resets the counters. |
| FA | Clock start, plus a UI "Play" event |
| FB | Play event if the transport is stopped or paused |
| FC | Stop event if playing. Also when Arm to Start (`settings+0x59`) is on and the master runs (`ui+0x13 == 2`): the silent follow ends (sequencer.md 2.9) |
| F8 | One clock tick: updates the tempo measurement and reloads the clock-loss timeout `0x20000084 = 20`, i.e. 2 s |

Forwarding of incoming FA/FB/FC/F8:

| Sync source | To USB | To DIN |
|---|---|---|
| MIDI (3) | yes | yes if Thru is off; if Thru is on, the Thru path already sends it |
| USB (1) | no | yes if Thru is off; if Thru is on, the Thru path already sends it |
| Internal / Sync In | – (ignored) | – |

In short, an external clock is always passed on to DIN, and a DIN clock is also passed on to USB.

## 6. External transpose — `ext_transpose_process` `0x08012e88`

- The transpose table (+0x64c) uses the transpose channel (code 0x21) and the port mask (code 0x27).
- **While the seq/arp is playing**, the note chosen from the table is written to `0x20000058`. The sequencer offsets its playback by (note − centre note); the centre note is `settings+0x57`, default 60.
- **While not playing**, if the current note equals `0x20000058` and latch (`settings+0x52`) is 0, the value is reset to −1, which means no transpose.
- The manual says transpose input is ignored when MIDI Thru is on. The code does **not** check Thru here, so notes still feed the transpose table (?).

## 7. Sysex

### 7.1 Framing

The decoder `0x080107f8` accepts these frames:

| Frame | Meaning |
|---|---|
| `F0 00 20 6B <dev> 42 <cmd> <params…> F7` | Arturia command. `<dev>` must equal the device ID byte (+0x3e) or 0x7F. |
| `F0 7E 7F 06 01 F7` | Identity request |
| `F0 7F …` | Universal realtime, ignored |
| `F0 5A 57 6E 28 3C 4E 3F …` (key at `0x08019BE4`) | Special magic, mapped to internal cmd 0x26 (bootloader) |

Identity reply (`0x080129cc`), sent to USB:

```
F0 7E 7F 06 02  00 20 6B  02 00  08 00  1C 00 01 01  F7
                Arturia   family model  version 1.1.0.28 (LSB first)
```

The meaning of the model bytes `08 00` is a guess (?).

### 7.2 Command groups

| cmd | Handler | Purpose |
|---|---|---|
| 0x02 | `sysex_command_handler` `0x080120c4` | Set a parameter (see below) |
| 0x0C | `0x080120c4` → `0x0801137c` | Sequence data write |
| 0x21, 0x25 | `0x080120c4` → `0x080115bc` | Sequence data / pattern transfer (?) |
| 0x23 | `0x080120c4` | Play a test note |
| 0x26 | `0x080120c4` | Write the bootloader flag and reset into the updater |
| 0x01, 0x09, 0x0B, 0x20, 0x22, 0x24, 0x7F | `sysex_get_handler` `0x08011ed4` | Get / reply. `01 41 <idx>` replies with the global setting byte at `0x20005084+idx`. |
| 0x05–0x08, 0x0A, 0x0F | `sysex_calib_handler` `0x08011fd4` | Calibration (touch strips: `08 40 …`, steps '2'/'3'); saved via `0x08015f24` |

Sub-parameters of cmd 0x02:

| Param | Meaning |
|---|---|
| < 0x40 | Per-sequence / global parameter (`0x08011768`) |
| 0x41 `<idx> <value>` | Set the global setting with code `idx` (`settings_set_code` `0x080185b8`: clamps the value and notifies listeners). This is how MIDI Control Center changes settings. |
| 0x70–0x74 | Factory tests. 0x73 drives the CV outputs directly: pitch 0x10, mod 0x11, gate 0x12. |
| 0x7A, 0x7D | Diagnostic flags, including test mode at `settings+0xa9` |

All replies go through the queue at `0x2000025C` to USB only.

## 8. Main-loop work related to MIDI

| Function | Rate | Job |
|---|---|---|
| `midi_usb_parse_pending` `0x080128ae` | 2× per loop, ≤50 bytes each | USB bytes → parser |
| `midi_din_rt_drain` `0x0801284c` | ≤20 per loop | DIN messages → clock handler |
| `midi_to_cv_process` `0x08012ee4` | every loop | MIDI note → CV |
| `ext_transpose_process` `0x08012e88` | every loop | transpose note |
| `midi_out_service` `0x0801674c` | every >4 ticks (≈500 µs) | USB IN transfers |
