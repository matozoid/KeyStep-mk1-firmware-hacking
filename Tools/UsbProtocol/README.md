# KeyStep USB Bootloader Protocol Kit

Goal: capture how Arturia MIDI Control Center uploads a `.led` firmware to the KeyStep over USB, in order to replicate that protocol with a custom Python loader.

## Important

This does NOT load custom firmware yet. It first captures and analyzes the protocol.

Workflow:

1. Put the KeyStep in bootloader mode.
2. Capture an official update with Wireshark + USBPcap.
3. Extract the USB OUT packets.
4. Compare them against the official `.led`.
5. With that evidence, build the real Python loader.

## Requirements

- Windows recommended for this stage.
- Wireshark with USBPcap.
- Arturia MIDI Control Center.
- Official `.led` firmware.
- KeyStep connected over USB.

Wireshark can capture USB; on Windows, USBPcap is used as the capture backend.

## Recommended capture on Windows

1. Install Wireshark and check the USBPcap option during installation.
2. Disconnect the KeyStep.
3. Open Wireshark.
4. You will see interfaces like `USBPcap1`, `USBPcap2`, etc.
5. Start a capture on a USBPcap interface.
6. Put the KeyStep in bootloader mode:
   - disconnect the KeyStep;
   - hold Play + Stop + Record;
   - connect USB without releasing;
   - wait a few seconds.
7. Open Arturia MIDI Control Center.
8. Load the official `.led` firmware.
9. Stop the capture when it finishes.
10. Save it as:

```text
keystep_update.pcapng
```

If you don't know which USBPcap interface it was, repeat with another one, or capture each available USBPcap interface one by one.

## Extract payloads with this kit

Install `tshark` along with Wireshark. Then:

```bash
python extract_usb_payloads.py keystep_update.pcapng --out out_payloads
```

The script will try to extract `usb.capdata`, `usb.data_fragment` and `usbhid.data`.

## Compare against the .led

```bash
python compare_led_to_capture.py keystep_Firmware_Update__1_0_28.led out_payloads
```

What we're looking for:

- Whether the `.led` appears as ASCII hex text.
- Whether the decoded binary payload of the `.led` appears.
- Whether it appears in blocks with headers/checksums.
- Whether the traffic uses MIDI SysEx or a different USB protocol.

## File to share

Once you have the capture, share:

```text
keystep_update.pcapng
```

or, if it is too large:

```text
out_payloads/
```

compressed as a ZIP.

With that, the real protocol can be inferred.
