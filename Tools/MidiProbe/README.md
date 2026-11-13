# KeyStep Linux MIDI Probe

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

It does not write firmware.

## Inspect the .led

```bash
python3 keystep_led_inspect.py keystep_Firmware_Update__1_0_28.led
```

## What to report back

Share the full output of:

```bash
python3 keystep_bootloader_probe.py --identity
```

Depending on the response, we determine whether the bootloader accepts standard SysEx commands or whether proprietary commands need to be discovered.
