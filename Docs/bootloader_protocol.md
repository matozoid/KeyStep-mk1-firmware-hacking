# Bootloader / uploader protocol

## Entering the bootloader

Observed method:

```text
Hold Record + Stop + Play/Pause while connecting USB
```

## Observed USB/MIDI identification

In bootloader mode:

```text
amidi -l: IO hw:2,0,0 KeyStep Updater MIDI 1
lsusb: ID 1c75:0208 Arturia KeyStep Updater
```

In normal mode:

```text
amidi -l: IO hw:2,0,0 Arturia KeyStep 32 MIDI 1
```

## Observed handshake

```text
Host → KeyStep:
F0 5A 57 6E 28 3C 4E 3F F7

KeyStep → Host:
F0 3F F7
```

The start sequence is sent twice.

## Sending frames

- `.led` file: ASCII hex
- Observed raw frame: 2078 bytes
- Frame as ASCII hex: 4156 characters
- Each payload is sent as SysEx:

```text
F0 <ASCII HEX> F7
```

Frame ACK:

```text
F0 1A F7
```

Final ACK:

```text
F0 77 F7
```

## Uploader used

```bash
python3 tools/uploader/keystep_rawmidi_handshake_uploader.py firmware.led --port hw:2,0,0
```
