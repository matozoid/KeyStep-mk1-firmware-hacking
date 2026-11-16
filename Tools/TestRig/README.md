# KeyStep USB test rig

`keystep_testrig.sh` flashes a firmware image (optional), then checks that the KeyStep still works over USB.

Each run:
1. logs the USB descriptors;
2. sends a MIDI Identity Request and logs the reply;
3. routes the KeyStep into FluidSynth so you hear what you play;
4. logs every incoming MIDI event with a timestamp to `vendor/testrig/session-*.log` (gitignored).

```bash
Tools/TestRig/keystep_testrig.sh                                  # play test only
Tools/TestRig/keystep_testrig.sh --flash Firmware/build/app.bin   # pack, flash, play test
Tools/TestRig/keystep_testrig.sh --flash vendor/keystep_1_0_28.led  # restore stock firmware
```

## Options

| Option | Meaning |
|---|---|
| `--flash FILE` | Flash before testing. A `.bin` must be linked at 0x08004000; the rig packs it into a `.led` using `--template`. |
| `--template FILE.led` | Template for packing a `.bin` (default `vendor/keystep_1_0_28.led`). |
| `--flash-only` | Stop after flashing. |
| `--yes` | Skip the `flash` confirmation prompt. |

## Flashing

The rig asks you to start the KeyStep in bootloader mode: hold **Record + Stop + Play** while plugging in USB. It shows up as `1c75:0208 KeyStep Updater`. The rig then uploads the image with `Tools/Uploader` and asks you to replug normally. The bootloader is never written, so a bad image can always be recovered by flashing the stock `.led`.

## Requirements

- `fluidsynth` and a GM soundfont (default `/usr/share/sounds/sf2/FluidR3_GM.sf2`; override with `SF2=`).
- alsa-utils (`amidi`, `aconnect`, `aseqdump`), `lsusb`, `python3`.

## Tuning

| Variable | Default | Effect |
|---|---|---|
| `FLUID_DRIVER` | `pipewire` | FluidSynth audio driver (`pulseaudio`, `alsa`, …) |
| `FLUID_PERIOD` | `128` | Audio period in samples. Raise it if the sound crackles. |

Without FluidSynth, the rig falls back to TiMidity++ through libao, which lags by about 0.5 s. TiMidity's direct ALSA output (`-Os`) segfaults inside libasound under PipeWire.
