# KeyStep Linux MIDI probe

A KeyStep MK1 in bootloader mode shows up like this:

```text
ID 1c75:0208 Arturia KeyStep Updater
hw:2,0,0 KeyStep Updater MIDI 1
```

This confirms that the bootloader exposes a USB-MIDI port on Linux.

## Install requirement

```bash
sudo apt install alsa-utils
```

## List ports

```bash
python3 keystep_bootloader_probe.py --list
```

## Test a safe response

With the KeyStep in bootloader mode:

```bash
python3 keystep_bootloader_probe.py --identity
```

This only sends the standard MIDI Universal Device Inquiry SysEx:

```text
F0 7E 7F 06 01 F7
```

It does not write firmware. The bootloader's real protocol is in
[`Docs/bootloader_protocol.md`](../../Docs/bootloader_protocol.md).

## Inspect the .led

```bash
python3 keystep_led_inspect.py vendor/update.led
```
