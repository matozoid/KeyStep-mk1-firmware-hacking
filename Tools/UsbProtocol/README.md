# USB capture analysis

Tools used to work out the update protocol by capturing an official update. The result is documented
in [`Docs/bootloader_protocol.md`](../../Docs/bootloader_protocol.md). They are only needed again to
check a new bootloader or updater version.

| Script | Purpose |
|---|---|
| `extract_usb_payloads.py` | Pull USB OUT payloads (`usb.capdata`, `usb.data_fragment`, `usbhid.data`) from a pcapng via `tshark` |
| `compare_led_to_capture.py` | Check whether the `.led` contents (ASCII hex or decoded) appear in the captured payloads |
| `midi_sysex_monitor.py` | Log SysEx traffic on a MIDI port (mido) while the official updater runs |

## Capturing an update (Windows)

1. Install Wireshark with the USBPcap option.
2. Start a capture on the USBPcap interface the KeyStep is on.
3. Put the KeyStep in bootloader mode: hold Record + Stop + Play while plugging in USB.
4. Run the update from Arturia MIDI Control Center with the official `.led`.
5. Stop the capture and save it, e.g. `vendor/keystep_update.pcapng`.

## Analysing

```bash
python extract_usb_payloads.py vendor/keystep_update.pcapng --out vendor/out_payloads
python compare_led_to_capture.py vendor/update.led vendor/out_payloads
```

Keep captures in `vendor/` — they contain the official firmware.
