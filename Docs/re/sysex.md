# SysEx protocol (MIDI Control Center)

This document describes how stock firmware 1.0.28 receives and answers SysEx. MIDI Control Center (MCC) uses it to read and write settings and sequences, to calibrate the touch strips, to run factory tests, and to start the updater. Unconfirmed inferences are marked (?). Settings codes `p` are defined in settings.md.

## 1. Transport

- **USB only.** Both the USB and the DIN parser contain a SysEx state machine, but the dispatcher `midi_in_msg_dispatch` 0x08012a88 ignores everything except context 0 (USB). SysEx sent to the DIN input is dropped.
- The USB parser lives at MIDI-in object `0x2000471C` +0x58 and the DIN parser at +0x248. The ctor is 0x08012c40.
- Byte parser: `sysex_parser_byte` 0x080107f8. Parsed messages go into a struct at parser+0x44.
- All replies are queued and sent on USB IN only.
  - Arturia-format replies: 5 slots × 0x72 bytes.
  - Universal replies: 3 slots × 0x10 bytes.
- `midi_out_service` 0x0801674c sends **one queued SysEx per service tick**, and gives SysEx priority over normal MIDI traffic.

## 2. Frame format

```
F0 00 20 6B <dev> 42 <cmd> <b7> [<b8> [<idx…>] [<value|data>]] F7
```

| Field | Notes |
|---|---|
| `00 20 6B` | Arturia manufacturer ID |
| `dev` | Must equal the parser's device ID or 0x7F. Requests from MCC use 0x7F. The stored ID appears to be 0x10 (?). Replies always carry 0x7F. |
| `42` | Fixed product/protocol byte. Any other value aborts the message. |
| `cmd` | Command, §3. cmds 0x03, 0x04, 0x07, 0x0D and 0x0E are rejected by the parser. |
| `b7` | Address byte 1. For settings it is 0x00. |
| `b8` | Address byte 2. 0x41 = global setting, 0x60–0x66 = sequence fields, < 0x40 = control/test. |
| `idx…` | 1 index byte, or more depending on the command (below) |
| value | 1 byte. For cmd 0x09/0x0A it is 14 bits, sent as two 7-bit bytes, MSB first. |

How the parser lays out the rest of the message:

| cmd | After `b7` |
|---|---|
| 0x05, 0x06, 0x1C | nothing; the message ends after `b7` |
| 0x0B, 0x0C (sequence block) | `b8`, then a count n (0..3), n index bytes, a length L (≤100), then L data bytes |
| 0x20/0x21 | `b8`, idx, then 1 extra index |
| 0x22/0x23 | `b8`, idx, then 2 extra indices |
| 0x24/0x25 | `b8`, idx, then 3 extra indices |
| 0x0F | `b8`, idx, a 14-bit word, 1 byte |
| others | `b8`, idx |
| value follows for | 0x02, 0x0A, 0x0C, 0x21, 0x23, 0x25 |

If a frame is malformed or truncated, the parser generates internal cmd 0x7F, which the get handler turns into an ACK. Any 0xF0 inside a message restarts the parser.

### Replies

The reply builder is `sysex_send_arturia` 0x08016bec:

```
F0 00 20 6B 7F 42 <cmd> <b7> <b8> [idx…] [value | data] F7
```

ACK (`sysex_send_ack` 0x0801174c): `F0 00 20 6B 7F 42 1C 00 F7`.

## 3. Command dispatch

| cmd | Direction | Handler | Meaning |
|---|---|---|---|
| 0x01 | host→dev | `sysex_get_handler` 0x08011ed4 | Get a parameter, reply cmd 0x02 |
| 0x02 | both | `sysex_command_handler` 0x080120c4 | Set a parameter / get-reply |
| 0x05, 0x06, 0x07, 0x0A, 0x0F | host→dev | `sysex_cal_handler` 0x08011fd4 | Accepted but no effect, apart from 0x08 (?) |
| 0x08 | host→dev | 0x08011fd4 | Touch-strip calibration, §5.6 |
| 0x09 | host→dev | get | Byte8 '0'/'2', reply cmd 0x0A, meaning unknown (?) |
| 0x0B | host→dev | get, 0x080114cc | Sequence block read, reply cmd 0x0C |
| 0x0C | both | `seq_block_write` 0x0801137c | Sequence block write / block reply |
| 0x1C | dev→host | — | ACK |
| 0x20/0x22/0x24 | host→dev | get | Multi-index get, reply 0x21/0x23/0x25 |
| 0x21/0x25 | both | `seq_param_set` 0x080115bc | Set one sequence value |
| 0x23 | both | 0x080120c4 | Note preview (§5.5) |
| 0x26 | internal | 0x080120c4 | Bootloader request, §6 |
| 0x7F | internal | get | Parse error, sends ACK |

## 4. Global settings

| Action | Request | Device response |
|---|---|---|
| Get | `F0 00 20 6B 7F 42 01 00 41 <p> F7` | `F0 00 20 6B 7F 42 02 00 41 <p> <value> F7`, then the ACK |
| Set | `F0 00 20 6B 7F 42 02 00 41 <p> <value> F7` | none |

The set path does the following:
- p = 0 is ignored.
- Otherwise it calls `settings_set(p, value)`, which clamps per settings.md §3 and notifies listeners.
- It refreshes the user-channel mirror `0x20000052`.
- It sets the "settings changed" flag. The settings are written to flash on the next 20 s tick (settings.md §5).

For a get, `value` is the raw stored byte. Channels are 0-based and 0x41 means "User".

A get with `b8 < 0x40` replies with value 0.

## 5. Other `cmd 0x02` sub-functions

`b7 = 0x00` unless noted otherwise.

### 5.1 Control (`b8 < 0x40`) — `sysex_ctrl_set` 0x08011768

| b8 | idx | value | Effect |
|---|---|---|---|
| 0x10 | LED number | 0/1 | Sets that LED (0x0800ce1c). The value bit is inverted. |
| others | — | — | none found (?) |

### 5.2 Factory loopback tests (need a test jig)

| b8 | Function | Behaviour |
|---|---|---|
| 0x70 | `test_din_loop` 0x08011148 | Sends `B5 55 4A` on the DIN out and expects it back on the DIN in |
| 0x71 | `test_sync_clock_loop` 0x08010ef0 | Toggles the sync clock out and checks the clock in. On pass, replies with the CC packet `B0 02 71` on USB. |
| 0x72 | `test_sync_start_loop` 0x08010f40 | Same for the start/stop line. Pass replies `B0 02 72`. |
| 0x74 | `test_pedal_adc` 0x08010fe4 | Pedal ADC average must lie in 0x971..0x997. Pass replies `B0 02 74`. |

### 5.3 Direct CV drive (`b8 = 0x73`)

| idx | Effect |
|---|---|
| 0x10 | Pitch CV set to note `value` |
| 0x11 | Mod CV set from `value`, scaled to a 0x319C mV (12.7 V) full range |
| 0x12 | Gate on/off. Values 1 and 2 also select the gate level mode. |

### 5.4 Diagnostic flags (`b7 = 0x7D, b8 = 0x7D`)

| idx | Effect |
|---|---|
| 0x03 | Test flag bit0 (+0xAC) |
| 0x09 | Test mode on/off (settings +0xA9). Entering it (`test_mode_enter` 0x08010f90) forces gate type 1 and redirects the strips and pedal to report over SysEx. |
| 0x0A | Test flag bit1 |
| 0x0B | LED test pattern (0x08013776) (?) |

`settings_test_flag_set` 0x08018588 has a bug: clearing a flag ANDs with the mask instead of with its inverse, so it clears all the other flags too.

### 5.5 Note preview (?)

| Frame | Effect |
|---|---|
| `02 7D 7A 06 <note>` | Note-on, velocity 0x40, sent to the CV voice |
| `23 7D 7A 06 <x> <note> <vel>` | Note-on with an explicit velocity |

### 5.6 Touch-strip calibration (cmd 0x08)

| Frame | Effect |
|---|---|
| `08 40 32` with idx 0x41 | Run a calibration step on the pitch strip (0x08011234) |
| `08 40 32` with idx 0x40 | Same for the mod strip |
| `08 40 33 …` | Second calibration step, 0x0801122a (?) |

When both strips report done (0x080111e6), the calibration is saved to `0x0803F200` (settings.md §7).

## 6. Sequence transfer

Index bytes: `[0]` = 1, `[1]` = sequence 1–8, `[2]` = step 0–63, `[3]` = note slot 0–7. Commands use as many of these as they need (?).

| b8 | Field | Sequence RAM |
|---|---|---|
| 0x60 | Length | +0x400 |
| 0x61 | Swing (?) | +0x401 |
| 0x62 | Gate length (?) | +0x402 |
| 0x63 | Tie bits; one 32-flag block (?) | — |
| 0x65 | Note pitches | step × 16 + slot × 2 |
| 0x66 | Velocities (?) | step × 16 + slot × 2 + 1 |

A sequence is 0x408 bytes: 64 steps × 8 slots × (note, velocity), then +0x400 length, +0x401, +0x402 and the flags at +0x403. In a slot, note 0xFF = empty and 0x82 = rest. See sequencer.md.

| Op | Frame | Handler |
|---|---|---|
| Block read | `0B <b7> <b8> <n> <idx…>`, reply `0C … <len> <data…>` | 0x080114cc |
| Block write | `0C <b7> <b8> <n> <idx…> <len> <data…>` (len ≤ 100) | 0x0801137c |
| Single value | `21`/`25 <b7> <b8> <idx…> <value>` | 0x080115bc |

After a sequence change, the sequence is marked dirty with a 10 s debounce, and its flash page is rewritten (settings.md §5).

## 7. Universal messages

| Message | Response |
|---|---|
| Identity Request `F0 7E 7F 06 01 F7` (device must be 0x7F) | `F0 7E 7F 06 02 00 20 6B 02 00 08 00 1C 00 01 01 F7` |

The reply is built by `sysex_identity_handler` 0x080129cc and sent via `sysex_send_universal` 0x08016d38. Its fields:
- family 0x0002;
- model 0x0008;
- 4 version bytes `1C 00 01 01`. They come from the words at 0x08019BD4, which are {1, 1, 0, 28}, sent in reverse order (?).

## 8. Updater entry

| Item | Value |
|---|---|
| Trigger | The raw byte sequence `F0 5A 57 6E 28 3C 4E 3F` (signature at 0x08019BE4). It is not an Arturia-format frame. |
| Parser result | internal cmd 0x26 (`'&'`) |
| Action | `bootloader_flag_write` erases page `0x0803F800` and programs the two words 0xA33A95FF, 0xAAA55A55 (from 0x0801A27A) |
| Reset | No reset in this path was found. The host is probably expected to power-cycle the unit, or a reset happens elsewhere (?). |

The bootloader at 0x08000000 is presumed to check those words at power-up and stay in update mode (?). An open firmware must keep this behaviour if it wants MCC-based updates and recovery.
