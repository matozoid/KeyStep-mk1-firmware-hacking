# USB device

This document describes the USB device implemented by stock firmware 1.0.28. The firmware uses the ST USB device library on the STM32F103 full-speed peripheral, with a class-compliant USB-MIDI 1.0 class and no vendor-specific interfaces. Unconfirmed inferences are marked (?).

## 1. Where the descriptors live

The descriptors are not stored in flash in readable form. They are part of the initialised RW data, which the scatter loader decompresses into RAM at start-up:

| Scatter entry (table at 0x0801ACD4) | Value |
|---|---|
| Source | 0x0801AD04 (compressed) |
| Destination | 0x20000000, 0x260 bytes |
| Decompressor | 0x080042fc (armlink LZ-style `__decompress`) |
| Then | zero-init 0x20000260, 0x97F0 bytes |

The decompressor reads a token byte `t`:
- `t & 7` = literal count + 1; if 0, the count is the next byte.
- `t >> 4` = match/zero length; if 0, the length is the next byte.
- Bit 3 set → back-reference: the next byte is the offset, and length + 2 bytes are copied.
- Bit 3 clear → that many zero bytes are written.

Decoded addresses:

| RAM | Content |
|---|---|
| 0x20000008 | Descriptor callback table (ST `USBD_DescriptorsTypeDef`), see §3 |
| 0x20000024 | Device descriptor (18 bytes) |
| 0x20000038 | LangID string descriptor |
| 0x20000120 | Class callback table (ST `USBD_ClassTypeDef`, 14 entries) |
| 0x20000160 | Configuration descriptor (101 bytes) |
| 0x200001C8 | Device qualifier (10 bytes) |
| 0x200001D2 | USB-MIDI CIN → byte-count table (16 bytes) |
| 0x20000280 | Scratch buffer for UTF-16 string descriptors |

## 2. Device descriptor

Bytes: `12 01 00 02 00 00 00 40 75 1C 88 02 00 02 01 02 03 01`

| Field | Value |
|---|---|
| bcdUSB | 2.00 |
| Class / Subclass / Protocol | 0 / 0 / 0 (defined per interface) |
| bMaxPacketSize0 | 64 |
| idVendor | **0x1C75** (Arturia) |
| idProduct | **0x0288** |
| bcdDevice | 2.00 |
| iManufacturer / iProduct / iSerialNumber | 1 / 2 / 3 |
| bNumConfigurations | 1 |

Device qualifier: `0A 06 00 02 00 00 00 40 01 00`. Its fields match the device descriptor. A full-speed-only device does not need it.

## 3. Strings

The strings are stored as ASCII and converted to UTF-16 at request time (`USBD_GetString` 0x0800aad0). Each string getter returns the same text for both speeds.

| Index | Getter | Text |
|---|---|---|
| 0 | 0x0800a820 | LangID 0x0409 (`04 03 09 04`) |
| 1 | 0x0800a830 | `Arturia` |
| 2 | 0x0800a850 | `Arturia KeyStep 32` |
| 3 | 0x0800a88c | `00000000001A` (fixed, the same on every unit) |
| config | 0x0800a7a0 | `AUDIO Config` (not referenced by the config descriptor, iConfiguration = 0) |
| interface | 0x0800a7e8 | `AUDIO Interface` (not referenced) |

The device descriptor getter is 0x0800a7d8.

Hosts show the port name "Arturia KeyStep 32", which is the product string. The jacks have no string index.

## 4. Configuration descriptor (101 bytes)

```
09 02 65 00 02 01 00 80 32
09 04 00 00 00 01 01 00 00
09 24 01 00 01 09 00 01 01
09 04 01 00 02 01 03 00 00
07 24 01 00 01 41 00
06 24 02 01 01 00
06 24 02 02 02 00
09 24 03 01 03 01 02 01 00
09 24 03 02 04 01 01 01 00
09 05 02 02 40 00 00 00 00
05 25 01 01 01
09 05 81 02 40 00 00 00 00
05 25 01 01 03
```

| Part | Meaning |
|---|---|
| Configuration | 2 interfaces, value 1, no string, bus-powered (0x80), 100 mA (0x32) |
| Interface 0 | Audio / AudioControl, 0 endpoints |
| AC header | ADC 1.00, total 9 bytes, 1 streaming interface (interface 1) |
| Interface 1 | Audio / MIDIStreaming, 2 endpoints |
| MS header | MS 1.00, total class-specific length 0x41 |

Jack layout (USB-MIDI 1.0, a single cable 0):

| ID | Type | Kind | Source | Role |
|---|---|---|---|---|
| 1 | IN | Embedded | — | Data from the host (EP 0x02) enters the device |
| 2 | IN | External | — | Notional physical input |
| 3 | OUT | Embedded | jack 2 | Data to the host (EP 0x81) |
| 4 | OUT | External | jack 1 | Notional physical output |

Endpoints:

| EP | Dir | Type | wMaxPacketSize | bInterval | Associated jack |
|---|---|---|---|---|---|
| 0x02 | OUT | Bulk | 64 | 0 | embedded IN jack 1 |
| 0x81 | IN | Bulk | 64 | 0 | embedded OUT jack 3 |

The endpoint descriptors are 9 bytes long (audio-class form, with bRefresh = 0 and bSynchAddress = 0).

The 5-pin DIN port is **not** exposed as a separate USB cable. The device has one virtual port, and routing between USB and DIN is done in firmware (midi.md).

## 5. Class driver

The class callback table at 0x20000120:

| Callback | Address | Behaviour |
|---|---|---|
| Init | 0x0800a570 | Opens EP 0x81 and EP 0x02 as bulk, 64 bytes, and arms the receive on EP 0x02 into buffer `0x20000BC4` (64 B) |
| DeInit | 0x0800a528 | Closes 0x81 and 0x02 |
| Setup | 0x0800a5b8 | Standard and class requests (no MIDI-specific requests) (?) |
| EP0 TxSent / RxReady, DataIn, DataOut, SOF, ISO in/out incomplete | 0x0800a548 / 0x0800a542 / 0x0800a51c / 0x0800a522 / 0x0800a5b0 / 0x0800a5a4 / 0x0800a5aa | Empty stubs that return OK |
| Get HS / FS / other-speed config | 0x0800a550 | All return the same 101-byte descriptor |
| Get device qualifier | 0x0800a560 | Returns the 10-byte qualifier |

USB clock and pins:
- 48 MHz from the PLL (72 MHz / 1.5).
- The D+ pull-up is switched by PA8 (hardware_map.md).
- IRQs: `USB_LP_CAN1_RX0_IRQHandler` 0x0800bda8 and `USB_HP_CAN1_TX_IRQHandler` 0x0800bd98.

### Receive path (OUT EP 0x02)

1. The class DataOut is a stub. The work is done in the HAL data-out callback override 0x08006b88, which runs in the IRQ:
   - reads the received byte count;
   - if it is non-zero, passes the buffer to 0x080198dc → `usbmidi_rx_push` 0x08012740;
   - re-arms EP 0x02 with 64 bytes.
2. `usbmidi_rx_push` handles 4-byte USB-MIDI event packets:
   - It ignores the cable number.
   - It uses the CIN (low nibble of byte 0) to look up how many MIDI bytes follow (table at 0x200001D2: CIN 0,1 → 0; 2 → 2; 3,4 → 3; 5 → 1; 6 → 2; 7–0xB → 3; 0xC,0xD → 2; 0xE → 3; 0xF → 1).
   - It appends those bytes to a 256-byte ring at MIDI-in +0x140.
   - If fewer than 80 bytes are free, the **whole USB packet is dropped**, without NAK or retry.
3. The main loop (`midi_in_usb_poll` 0x080128ae, called twice per pass) feeds up to 50 bytes per call from the ring into the USB MIDI parser, which handles running status, SysEx and so on.

As a result, USB input is turned back into a byte stream and parsed like DIN. Packet boundaries and cable numbers are lost.

### Transmit path (IN EP 0x81)

- **Normal messages.** `midi_out_usb` 0x080169c4 pushes 4-byte messages into a 36-entry ring.
  - `midi_out_service` 0x0801674c runs from the main loop when more than 4 HAL ticks have passed since its last run.
  - If no SysEx reply is queued, 0x08016870 drains up to 16 messages (64 bytes) from the ring into `0x2000519A` and sends them with one HAL transmit on EP 0x81.
  - Packing (0x080167e8): cable 0; CIN = status high nibble for channel messages; CIN 0x2 for F1/F3; 0x3 for F2; 0xF for every other system byte, as single-byte packets.
- **SysEx replies** take priority, one queued message per service tick. They are packed by `usbmidi_put_sysex_byte` 0x08016e68 and `usbmidi_sysex_end_flush` 0x08016dd4: CIN 4 for each 3-byte chunk, then CIN 5/6/7 for the final 1/2/3 bytes. The rest of the 64-byte buffer is padded with zero packets and the buffer is sent as one transfer.
- There is no check that the previous IN transfer has finished before the next one is submitted; the HAL is assumed to cope (?).

## 6. Notes for an open firmware

- Re-using VID 0x1C75 / PID 0x0288 and the "Arturia KeyStep 32" product string keeps MCC and existing DAW port mappings working. MCC probably matches on VID/PID and on the identity reply (sysex.md §7) (?).
- The fixed serial number `00000000001A` means two KeySteps on one host look identical apart from their port path.
- The descriptor set is a plain single-cable USB-MIDI 1.0 device, so any class-compliant stack (TinyUSB, libopencm3) can reproduce it byte-for-byte.

## 7. Checked on hardware

Checked on 2026-11-16 against a unit running stock firmware 1.1.0.28, with `Tools/TestRig/keystep_testrig.sh`: about 32,000 events from four sessions, all played by hand.

| Item | Result |
|---|---|
| Enumeration | `1c75:0288`, "Arturia KeyStep 32", serial `00000000001A`, bus-powered at 100 mA. The interfaces, jacks and endpoints (EP 0x02 OUT and EP 0x81 IN, both bulk, 64 bytes) match §2–§4. Linux logs "Quirk or no altset; falling back to MIDI 1.0", which is harmless. |
| ALSA | One port, "Arturia KeyStep 32 MIDI 1". |
| Identity reply | `F0 7E 7F 06 02 00 20 6B 02 00 08 00 1C 00 01 01 F7`, byte-identical to sysex.md §7. The version reads 1.1.0.28, sent in reverse byte order, and matches the MCC `.led` version. |
| Channel | Every channel message came on channel 1 (the factory default). |
| Notes | 1615 note-ons and 1615 note-offs, so none were stranded. Note-on velocity covered 1–124, and every note-off had velocity 64 (keyboard.md §1). Notes 40–100 were seen, which needs octave shifts. |
| Aftertouch | Channel pressure (`Dn`) over the full range 0–127. This is the busiest message type by far after clock. |
| Pitch strip | Pitch bend from −8064 to +8191. The minimum is v = 1, not 0 (keyboard.md bend table), which points to strip calibration rather than firmware. |
| Mod strip | CC 1 (default of setting 0x14). |
| Transport | Play sends CC 54 = 127 and then `FA` Start (or `FB` Continue after a pause), then CC 54 = 0 on release. Stop sends CC 51 = 127 and then `FC`, then CC 51 = 0. Record sends CC 50 = 127 when it turns on and 0 when it turns off. The CC numbers and values match the settings defaults 0x64–0x66 (settings.md) and midi.md §4.1. |
| Clock | `F8` at 24 PPQN with the sync switch on Internal, sent continuously, also while stopped (midi.md §5.1). |
| Silent | The tempo, Mode and Time-division knobs and the Seq/Arp switch send no MIDI in normal mode. They act only on the device. |

Not checkable over USB: the CV, gate and sync outputs, and the DIN port.
