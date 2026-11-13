# Bootloader and update protocol

The Arturia bootloader occupies `0x08000000–0x08003FFF` and is never overwritten by an update.
It accepts a new application image (linked at `0x08004000`) over USB MIDI SysEx, so the official
firmware can always be restored with Arturia's updater.

## Entering the bootloader

Hold **Record + Stop + Play** while plugging in USB.

| Mode | USB ID | ALSA port |
|---|---|---|
| Bootloader | `1c75:0208 Arturia KeyStep Updater` | `KeyStep Updater MIDI 1` |
| Normal | — | `Arturia KeyStep 32 MIDI 1` |

## Session

```text
Host → KeyStep   F0 7E 7F 06 01 F7            identity request (optional, MCC sends it several times)
Host → KeyStep   F0 5A 57 6E 28 3C 4E 3F F7   handshake, sent twice
KeyStep → Host   F0 3F F7                     reply to each handshake
Host → KeyStep   F0 <22 hex chars> F7         firmware data; each .led frame is split over many of these
KeyStep → Host   F0 1A F7                     ACK after each frame
KeyStep → Host   F0 77 F7                     final ACK
```

### What MCC knows about the updater

MCC 1.23's device definition (`Resources/KeyStep.json`, see [re/settings.md](re/settings.md) §2a)
names the updater "midiplus" and gives:

| Field | Value |
|---|---|
| `updaterUsbVendorId` / `updaterUsbProductId` | 7285 / 520 (`1c75:0208`, matches the table above) |
| `firmwareFilePattern` | `*.led` |
| `productCode` | 63 (0x3F; probably the byte in the KeyStep's reply `F0 3F F7` (?)) |
| `productKey` | 90 87 110 40 60 78 63 = `5A 57 6E 28 3C 4E 3F`, the handshake payload |
| `minimalVersionRequired` | 1.0.0.5 |

MCC 1.23 bundles `keystep_Firmware_Update_1.1.0.28.led`, byte-identical to `vendor/keystep_1_0_28.led`.

## `.led` file format

The file is ASCII hex. Decoded, it is a sequence of frames (2078 bytes each):

| Part | Size | Content |
|---|---|---|
| Header | 18 bytes | `struct >7sH7sH`; field 1 = flash page, address = `0x08000000 + page << 8` |
| Data | 0x800 bytes | flash contents |
| Footer | 12 bytes | last 2 bytes = little-endian frame checksum |

An optional 12-byte tail follows the last frame. The image also carries an overall checksum at its
end. `Tools/Ledpacker/make_keystep_custom_led.py` recalculates all three.

## Tools

```bash
# unpack an official update for analysis
python3 Tools/Ledpacker/led_to_bin.py vendor/update.led vendor/app.bin

# pack your own app.bin (linked at 0x08004000), using an official .led as the template
python3 Tools/Ledpacker/make_keystep_custom_led.py vendor/update.led build/app.bin build/app.led

# upload (bootloader mode); --dry-run and --only-handshake are available for testing
python3 Tools/Uploader/keystep_rawmidi_handshake_uploader.py build/app.led --port hw:2,0,0
```
