# KeyFreak 🎹

**I turned an Arturia KeyStep Mk1 into a mini digital synthesizer that plays on its own.**

This repo documents how I did it: from opening the unit and finding the microcontroller, to capturing the upload protocol, figuring out how the keyboard is wired and writing custom firmware that generates audio. It is an experimental, educational project, built through trial and error. If you like embedded firmware, reverse engineering or DIY synths, I hope you find it useful.

> **Status:** working proof of concept. The KeyStep boots custom firmware, reads its 32 keys and makes sound through the Mod CV output. It is not a finished product and does not replace the official firmware.

- **Microcontroller:** STM32F103VCT6 (ARM Cortex-M3)
- **Custom loader:** `KS-BootBridge`
- **Working audio output:** Mod CV / DAC2 / PA5
- **Tools:** Ghidra · Wireshark/USBPcap · Python · GCC ARM Embedded · raw MIDI/SysEx
- **Author:** Mapache Bastardx

---

## ⚠️ Before you start: read this

Modifying the firmware of a commercial device **can brick it**. Everything here is experimental and done at my own risk; if you replicate it, it is at yours.

**Electrically:**
- Do not plug headphones directly into the CV outputs.
- Use a mixer, audio interface or synth input with the volume turned down.
- Do not solder permanent modifications without verifying pins and voltages.
- Do not power external modules from internal points without measuring first.

**Out of respect for Arturia and the law**, in this repo (and I ask the same of you if you fork it):
- I do not upload official `.led` firmware, binary dumps, or anything proprietary extracted from the manufacturer.
- I do not clone or commercially replace anything from Arturia, nor claim any affiliation with them.
- If a tool needs the official firmware, users supply their own **locally**; it is not redistributed.

Trademarks are used for descriptive purposes only.

---

## Table of contents

1. [The question that started it all](#1-the-question-that-started-it-all)
2. [The hardware inside](#2-the-hardware-inside)
3. [Entering bootloader mode](#3-entering-bootloader-mode)
4. [Capturing the protocol with Wireshark](#4-capturing-the-protocol-with-wireshark)
5. [The `.led` format and the KS-BootBridge loader](#5-the-led-format-and-the-ks-bootbridge-loader)
6. [What I found in Ghidra](#6-what-i-found-in-ghidra)
7. [The big discovery: the keyboard matrix](#7-the-big-discovery-the-keyboard-matrix)
8. [First audio and first keys](#8-first-audio-and-first-keys)
9. [Trying out synthesis engines](#9-trying-out-synthesis-engines)
10. [MultiEngine and hidden controls](#10-multiengine-and-hidden-controls)
11. [Mod CV as the main output](#11-mod-cv-as-the-main-output)
12. [Velocity via dual contacts](#12-velocity-via-dual-contacts)
13. [Version table](#13-version-table)
14. [Final firmware architecture](#14-final-firmware-architecture)
15. [An honest comparison with commercial synths](#15-an-honest-comparison-with-commercial-synths)
16. [Future work](#16-future-work)
17. [Repo structure](#17-repo-structure)
18. [Closing thoughts](#18-closing-thoughts)

---

## 1. The question that started it all

It all began with a fairly simple question:

> Can an Arturia KeyStep Mk1 run my own firmware and generate sound by itself, instead of just being a controller?

The KeyStep was designed as a MIDI/CV controller, but inside it has an STM32 microcontroller perfectly capable of running custom code, reading a keyboard, driving GPIO, using its DACs and generating audio. That question turned into a list of goals: identify the chip, understand the bootloader and the update format, load custom code without touching the official tools, read the physical keyboard, get audio out of the internal DAC, build simple synthesis engines, and document everything so others can follow the same path.

What follows is that journey, roughly in the order things fell into place (stumbles included, because that is where the interesting part is).

---

## 2. The hardware inside

Opening the unit and inspecting the board, the brain turned out to be an **STM32F103VCT6**:

| Feature | Approximate value |
|---|---|
| Core | ARM Cortex-M3 |
| Clock | up to 72 MHz |
| Flash | ~256 KB |
| RAM | ~48 KB |
| DAC | 2 channels, 12-bit |

That two-channel DAC is the key to making noise. Its outputs are:

```text
PA4 = DAC_OUT1 = DAC1
PA5 = DAC_OUT2 = DAC2
```

At first I assumed audio would come out of `Pitch CV / DAC1 / PA4`. **I was wrong.** On the unit I tested, that output was not usable for audio, and the one that ended up working was:

```text
Main audio = Mod CV / DAC2 / PA5
```

I point this out up front because it took me several iterations to figure it out, and I don't want you to waste time on the same hypothesis. More detail in [section 11](#11-mod-cv-as-the-main-output).

---

## 3. Entering bootloader mode

The KeyStep enters update mode if you hold a button combination while connecting it over USB:

```text
Record + Stop + Play/Pause + plug in USB
```

On Linux it shows up as a different device:

```text
USB:  Arturia KeyStep Updater
MIDI: KeyStep Updater MIDI 1
```

In this mode, the device listens for firmware via MIDI/SysEx messages. The problem was: how do I send them myself?

---

## 4. Capturing the protocol with Wireshark

To load custom firmware I needed to understand how the official updater talks to the KeyStep. So I captured the traffic of an official update with **Wireshark + USBPcap** and observed the flow:

```text
PC / updater
   ↓  SysEx handshake
KeyStep responds
   ↓  firmware frames
ACK per frame
   ↓
final ACK
```

The messages I identified were:

| Message | Bytes |
|---|---|
| Handshake (PC → KeyStep) | `F0 5A 57 6E 28 3C 4E 3F F7` |
| KeyStep response | `F0 3F F7` |
| Firmware frame | `F0 <22 chars ASCII hex> F7` |
| Per-frame ACK | `F0 1A F7` |
| Final ACK | `F0 77 F7` |

Wireshark is what unlocked the whole project: it showed me that it was MIDI/SysEx, let me find the handshake, understand the size and shape of the frames, see the ACKs and, ultimately, **reproduce the updater's behavior with my own code**. Without that capture I would have known how to build binaries, but would have had no reliable way to get them onto the device.

---

## 5. The `.led` format and the KS-BootBridge loader

The official firmware uses the `.led` extension. It is not a flat binary: it carries the data as **ASCII hex**. What I observed:

```text
Raw frame:        2078 bytes
ASCII hex frame:  4156 characters
Final tail:       12 raw bytes = 24 hex characters
```

With that I was able to build a packer that takes a custom binary and wraps it in a structure compatible with the bootloader, plus a loader I called **KS-BootBridge**, because it acts as a bridge between the custom firmware and the KeyStep's MIDI/SysEx bootloader.

What the loader does, step by step:

1. Opens the raw MIDI port.
2. Sends the handshake.
3. Waits for the KeyStep's response.
4. Reads/splits the `.led` frames.
5. Sends each frame as SysEx.
6. Waits for the ACK of each frame.
7. Waits for the final ACK.

Conceptual usage:

```bash
python3 ks_bootbridge.py firmware.led --port hw:2,0,0
```

---

## 6. What I found in Ghidra

I loaded the firmware into Ghidra as **ARM Cortex-M Thumb**, with one important detail: the application does not start at `0x08000000`, but after the bootloader region. The correct base is:

```text
0x08004000
```

With the base set correctly, the vector table made sense (stack pointer in RAM, reset vector pointing to `0x08004xxx`) and I could reconstruct the boot path:

```text
Reset trampoline → early init → runtime init → App_Main → peripheral init → main loop
```

I renamed functions as I went to understand the subsystems. Some of the most useful ones:

```text
App_Main
MX_SPI2_Init
MX_DAC_or_ADC_Init
MX_UART_MIDI_Init
MX_USB_Device_Init_like
InputScanner8_Init
InputScanner_SelectIndex
InputScanner_ReadValue
InputScanner_ResetSelectLines
KeyboardScanner_Update
MIDI_NoteOn_Build / NoteOff / ControlChange / PitchBend
```

That let me split the firmware into blocks: hardware init, keyboard scanning, MIDI, control values, sequencer, DAC/CV and USB. But what really mattered was hidden in a single line.

---

## 7. The big discovery: the keyboard matrix

The function that changed everything was this one:

```c
InputScanner8_Init(DAT_080196e4, DAT_080196e0, 8, DAT_080196dc, 8);
```

Reconstructing the signature:

```c
InputScanner8_Init(scanner, selectPort, selectShift, readPort, readShift);
```

Resolving the addresses:

```text
DAT_080196e4 = 0x20000F44   → scanner
DAT_080196e0 = 0x40011800   → GPIOE  (select lines)
DAT_080196dc = 0x40011400   → GPIOD  (read lines)
```

And there was the real keyboard wiring:

```text
PE8–PE15 = select
PD8–PD15 = read
```

### How it works electrically

The scanner is **active-low**: you drive all select lines high, pull a single one low, read the inputs, invert and store. Repeat for all 8 rows.

```c
for (row = 0; row < 8; row++) {
    GPIOE->ODR |= 0xFF00;              // all inactive
    GPIOE->ODR &= ~(1 << (8 + row));   // select row
    delay_small();
    rows[row] = ~(GPIOD->IDR >> 8) & 0xFF;
}
```

### From matrix to keys

The KeyStep has 32 keys and **each one has two contacts** (A and B). The numbers add up perfectly:

```text
32 keys × 2 contacts = 64
8 selects × 8 reads  = 64
```

The select lines are split into pairs (one A contact and one B contact per group of 8 keys):

```text
PE8/PE9   → group 0 (A/B)
PE10/PE11 → group 1 (A/B)
PE12/PE13 → group 2 (A/B)
PE14/PE15 → group 3 (A/B)
```

And the read lines (`PD8–PD15`) select the column. The formula for any key:

```text
group  = keyIndex / 8
column = keyIndex % 8

Contact A:  PE = 8 + group*2       ,  PD = 8 + column
Contact B:  PE = 8 + group*2 + 1   ,  PD = 8 + column
```

For example, key 0 is `PE8+PD8` (A) and `PE9+PD8` (B); key 31 is `PE14+PD15` (A) and `PE15+PD15` (B).

> **What I learned the hard way:** before this I tried mapping individual GPIO pins "blindly", and only got false positives. The keyboard **cannot** be read by looking at individual pins; you have to activate `PE8–PE15` one at a time and read `PD8–PD15`. That was the leap from erratic tests to a real reading.

---

## 8. First audio and first keys

With the loader and the matrix figured out, I validated things piece by piece:

- **V4 — writing to the DAC:** made the output toggle voltage. It worked → custom firmware could control the DAC.
- **V5 — basic audio:** generated a simple audible signal. The unit made sound → the bootloader → uploader → firmware chain was complete.
- **V22 — real scanning:** used `PE8–PE15` / `PD8–PD15`, and pressing keys produced beeps → the discovered matrix was correct.
- **V23 — one note per key:** assigned a frequency to each key and each one played its note. **This was the moment the KeyStep stopped being a controller and became an instrument.**

---

## 9. Trying out synthesis engines

This is where a lot of trial and error came in. An honest summary of what I tried:

- **Square wave:** extremely cheap on CPU, but harsh.
- **Wavetable / sine:** smoother, but weak and not very expressive.
- **Moog-ish** (dual saw + sub-oscillator + simple digital filter + envelope + saturation): useful as a lead/bass, though limited by the DAC and the CV output.
- **Polyphony (4 voices):** technically worked, but sounded like a dense blob. Several voices + sub + saturation over a single simple DAC output = muddy mix.
- **FM / Yamaha-ish:** simple frequency modulation. More digital and bright.
- **Rhodes-ish:** not convincing. A Rhodes needs dynamics, resonance, preamp, amplification and effects; you can't get that out of DAC/CV.
- **MicroFreak-ish** (wavetable morph + soft FM + wavefolding + slow movement + vibrato): the character that best suits this hardware.

The overall conclusion: instead of fighting the hardware by imitating complex analog timbres, it is better to embrace digital, which is where the STM32 and the DAC feel comfortable.

---

## 10. MultiEngine and hidden controls

In **V35** I combined several engines into a single firmware, so I no longer had to compile one version per timbre:

```text
1 = MicroFreak-ish digital
2 = Yamaha FM
3 = Moog lead
4 = Acid bass
5 = Digital bell/pad
```

Since I haven't mapped the panel buttons yet, controls are done via combinations with **the lowest key**:

| Combination | Action |
|---|---|
| Lowest + 1…5 | select engine (MicroFreak / FM / Moog / Acid / Bell-Pad) |
| Lowest + 6 / 7 / 8 | octave down / normal / up |
| Lowest + 9 / 10 / 11 | dark / normal / bright timbre |
| Lowest + 12 / 13 / 14 | vibrato off / normal / deep |
| Lowest + 15 | hold / latch |
| Lowest + 16 / 17 | velocity/accent off / on |

---

## 11. Mod CV as the main output

For several versions I took for granted that audio went through `Pitch CV / DAC1 / PA4`. Real-world testing corrected me: on my unit that output was **not** usable for audio, and the good one was `Mod CV / DAC2 / PA5`.

From **V37** onward the project assumes:

```text
Main audio = Mod CV / DAC2 / PA5
Pitch CV   = ignored / reserved for future testing
```

I'm flagging this clearly so nobody repeats my confusion.

---

## 12. Velocity via dual contacts

The fact that each key has **two contacts** opens the door to measuring how hard you play. The idea:

```text
Contact A triggers first, contact B afterwards.
The time difference approximates the force:
  short difference = hard strike
  long difference  = soft strike
```

This can be mapped musically to more volume, more brightness, more FM amount or a sharper attack. **V40** implemented a first accent-style approximation. It is still rough, but the principle works.

---

## 13. Version table

| Version | What I tried | Result |
|---|---|---|
| V4  | DAC toggling voltage | validated DAC control |
| V5  | Basic audio | validated audible output |
| V22 | Real PE/PD scanner with beeps | validated the matrix |
| V23 | One note per key | first working instrument |
| V24 | Smooth wavetable | less harsh |
| V25 | Moog-ish | more useful lead/bass |
| V26 | 4 voices | dense polyphony |
| V27 | Clean duo | clearer |
| V29 | Mono lead + portamento + arp | more usable |
| V30 | FM + arp | bright digital |
| V31 | FM without arp | more direct |
| V32 | Rhodes-ish | not convincing |
| V33 | Rhodes MK2-ish | better, but limited |
| V34 | MicroFreak-ish | the most suitable approach |
| V35 | MultiEngine | 5 engines working |
| V36 | MultiEngine + octave | more usable |
| V37 | Performance + Mod CV | confirmed Mod CV output |
| V38 | Mod CV audio + auxiliary Pitch | Pitch not useful |
| V39 | Mod CV only | corrected architecture |
| V40 | Velocity / accent | first dynamics |

---

## 14. Final firmware architecture

```text
Reset
 → clock init
 → GPIO init
 → DAC init
 → PE/PD matrix configuration
 → main loop:
      keyboard scan
      read A/B contacts
      detect combinations
      engine selection
      octave / timbre / vibrato / hold
      synthesis render
      write to DAC2 / PA5 / Mod CV
```

In code, the loop is as simple as:

```c
while (1) {
    KeyboardMatrix_ScanRows(rows);
    KeyState_Update(rows);
    Controls_Update();
    sample = Synth_Render();
    DAC2_Write(sample);
}
```

---

## 15. An honest comparison with commercial synths

This is **not** a MicroFreak, a MiniFreak or a Reface DX. A fair description would be:

> **KeyFreak = a DIY digital mono-synth on STM32, an experimental "MicroFreak-lite".**

**What it does have:** a physical 32-key keyboard read by custom firmware, several simple digital engines, the Mod CV output used as audio, octave/timbre/vibrato/hold control and a first approximation of velocity.

**What it doesn't have:** a dedicated audio codec, a real line output, an analog filter, built-in effects, powerful DSP, a full FM engine or commercial-quality polyphony.

An interesting future upgrade would be an **external PCM5102 DAC** over I2S (BCLK/LRCK/DATA), which would give less noise, better dynamic range and a clean line-out. The challenge is finding free I2S pins, modifying the hardware, writing the driver and validating a stable sample rate. Note: the PCM5102 does **not** connect to Mod CV; it is a separate audio path.

---

## 16. Future work

Things I'd like to keep exploring (and where contributions are welcome):

1. Map the actual panel buttons.
2. Map the touch strips.
3. Improve real velocity via dual contacts.
4. More musical presets and refine the MultiEngine.
5. A clean paraphonic mode.
6. Investigate I2S with PCM5102.
7. Safer installer + recovery mode.
8. Document the hardware with photos and diagrams.
9. Split the firmware into modules and add keyboard/DAC tests.
10. External MIDI control and audio demos.

---

## 17. Repo structure

```text
keystep-custom-synth/
├── README.md
├── LICENSE
├── docs/
│   ├── bootloader_protocol.md
│   ├── firmware_format_led.md
│   ├── ghidra_analysis.md
│   ├── keyboard_matrix.md
│   ├── modcv_audio_output.md
│   ├── firmware_versions.md
│   └── safety_and_legal.md
├── firmware/
│   ├── v04_dac_test/
│   ├── v05_audio_test/
│   ├── v22_keyboard_scan/
│   ├── v23_single_note/
│   ├── v35_multiengine/
│   ├── v39_modcv_performance/
│   └── v40_velocity/
├── tools/
│   ├── ks_bootbridge/
│   └── led_packer/
├── hardware/
│   ├── board_notes.md
│   ├── pinout.md
│   └── pcm5102_notes.md
└── images/
```

**What to include:** your own C code, your own Python scripts, documentation, your own photos of the board, your own diagrams, summarized Ghidra notes, the discovered pinout, build instructions and warnings.

**What NOT to include:** official `.led` firmware, proprietary dumps, binaries extracted from Arturia, captures containing sensitive redistributable information, or trademarks used as if they were your own.

---

## 18. Closing thoughts

KeyFreak shows that a "closed" controller like the KeyStep Mk1 can be turned into a small open synthesis platform. But for me the most valuable part wasn't that it made sound, it was having rebuilt the whole path:

```text
identify the hardware
 → understand the bootloader
 → capture the protocol
 → write a custom uploader
 → analyze the firmware in Ghidra
 → discover the keyboard matrix
 → load custom firmware
 → generate audio via Mod CV
 → build synthesis engines
```

It doesn't replace any modern commercial synth, and it doesn't try to. The point is the methodology — inspect, capture, analyze, get it wrong, document and build step by step — and to leave it open so someone else can pick it up where I left off.

If you replicate it, improve it or get stuck on a step, issues and forks are welcome. That's why I'm publishing it. 🙂

---

*Made by Eduardo Mansilla as a learning project on embedded firmware, reverse engineering and digital synthesis. Not affiliated with Arturia; trademarks are mentioned for descriptive purposes only.*
