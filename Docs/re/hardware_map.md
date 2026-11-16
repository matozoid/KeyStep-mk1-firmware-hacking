# KeyStep Mk1 hardware map (from stock firmware 1.1.0.28)

Derived by static analysis of the official update (`keystep_Firmware_Update__1_0_28.led`,
unpacked with `Tools/Ledpacker/led_to_bin.py`, analysed with `Tools/Ghidra`). Addresses are
in the application image (linked at `0x08004000`). Function names are the ones in
`symbols.tsv`. "(?)" marks an inference not yet confirmed on hardware.

## MCU and clocks

| Item | Value | Evidence |
|---|---|---|
| MCU | STM32F103VC (256 KB flash, 48 KB RAM) | |
| Bootloader | `0x08000000–0x08003FFF` (Arturia, SysEx updater) | |
| Application | `0x08004000`, VTOR set in SystemInit | vector table |
| Clock | HSE 8 MHz × 9 = 72 MHz, APB1 /2, flash latency 2, USB 48 MHz | `SystemClock_Config` 0x0800cd94 |
| SysTick | 10 kHz (100 µs tick, counter at 0x20000070): software timers, key velocity timing, LED blink | 0x08009d44 |
| Watchdog | IWDG, reload 0xFFF. Started by the application, not by the bootloader: the open firmware never starts or feeds it and runs without resets | 0x08009600 |

## Flash layout used by the app

| Address | Size | Content | Evidence |
|---|---|---|---|
| `0x0803B000 + n*0x800`, n=0..7 | 8 × 2 KB | 8 sequences (RAM copies at `0x200022C8 + n*0x408`) | `seq_flash_is_empty` 0x08015c08, `seq_flash_erase_all` 0x08015bd0 |
| `0x0803F000` | 0x80 bytes | global settings → RAM `0x20005084` | 0x08015cd0 |
| `0x0803F200` | 4 bytes | touch-strip calibration: pitch hi, pitch lo, mod hi, mod lo (default 0xBF/0x09) — see [settings.md](settings.md) §7 | 0x08015eb8 |
| `0x0803F800` | 8 bytes | bootloader request flag `0xA33A95FF, 0xAAA55A55`, written by SysEx `'&'`; the bootloader stays in update mode when it finds it (confirmed on hardware, sysex.md §8) | 0x08015c80 |

Holding both octave buttons at power-on is the factory reset (manual 3.1.7: resets everything). It
erases the sequence pages and rewrites the settings page with an erased first word, so settings return
to defaults while the touch calibration survives ([settings.md](settings.md) §6).

## Pins

### Keybed (32 keys, 2 contacts each → velocity)

| Function | Pins | Notes |
|---|---|---|
| Matrix select (outputs) | PE8–PE15 | one-hot, 8 lines |
| Matrix read (inputs) | PD8–PD15 | read `GPIOD->IDR >> 8`, inverted (active low) |

Object `KeyMatrix` @ `0x20000F44`, ctor 0x08016610, select 0x080165ea, read 0x080165b8.
Velocity = time between the two contacts; three 6-segment curves (Log / Lin / AntiLog in MCC)
built in `keyboard_init` 0x08017eb0. Scan/note logic: 0x08016f78.

### Buttons (8, shared read bus with keybed)

| Function | Pins |
|---|---|
| Row select 0 | PE7 low (PB2 high) |
| Row select 1 | PB2 low (PE7 high) |
| Read | PD8–PD11, active low |

Button `i` = row `i/4`, bit `i%4` (`button_bit_ctor` 0x0801511a, read 0x08015102).
Button objects (ctor 0x0800f9d2, handler press 0x0800f3a0 / release 0x0800f7f0, poll
0x0800f2d8 with 32-sample debounce):

| id | RAM obj | LED bit | Function (from handler) |
|---|---|---|---|
| 0 | 0x2000129C | 2 | Hold; Shift+Hold = Chord mode. LED 2 is also driven by the sustain pedal |
| 1 | 0x200012D0 | 7 | Shift (flag 0x2000005F; knob changes deferred until release, manual 4.3.4) |
| 2 | 0x20001304 | 4 | Oct − (flag 0x2000005E); Shift+Oct− = Transpose mode |
| 3 | 0x20001338 | 3 | Oct + (flag 0x2000005D); Shift+Oct+ = Kbd Play mode |
| 4 | 0x2000136C | 6 | Tap tempo / Rest / Tie (press handler feeds the tap object 0x200000FC, or enters a rest in step record) |
| 5 | 0x200013A0 | 5 | Record (ui_button_event case 5 toggles the record state); Shift+Rec = append |
| 6 | 0x200013D4 | 0 | Stop (3 presses → CC 123 All Notes Off on all 16 channels); Shift+Stop = clear last step |
| 7 | 0x20001408 | 1 | Play/Pause; Shift+Play = restart |

### Switches

| Function | Pins | Evidence |
|---|---|---|
| Sync source selector. Code = ~(PE3 \| PE2 << 1) & 3: 0 Internal, 1 USB, 2 Sync In, 3 MIDI | PE2, PE3 (2-bit, inverted) | 0x08018a46, 0x08018994 → clock `set_source` 0x0800e5d0 |
| Seq / Arp mode switch: high = Seq, low = Arp | PE4 | 0x08011cb4 → `ui_set_seq_arp` 0x08014070 |
| LED level select | PD4 | 0x0800f20c, polled every main-loop pass. High: LEDs at 90 % of the brightness setting; low: 20 %. Fades are described under LEDs below. The pin is the DC power jack detect: the factory test reads it as "DCin Plug" (sysex.md 5.4), so the LEDs run brighter on an adapter than on USB power. |

### Analog inputs (ADC1, 5-channel scan, DMA1 ch1)

| Rank | Pin / ch | Function | Evidence |
|---|---|---|---|
| 1 | PC0 / 10 | Tempo knob → 30.00–240.00 BPM | 0x080134d8, 0x0801360c |
| 2 | PC1 / 11 | Sustain/Hold pedal (digital with hysteresis, polarity sampled at boot) | 0x08011a88, 0x08011b04 |
| 3 | PC2 / 12 | Seq/Arp Mode knob, 8 detents: sequence 1–8, or arp Up / Down / Inclusive / Exclusive / Random / Order / Up×2 / Down×2 | 0x08011da4 |
| 4 | PC3 / 13 | Time-division knob, 8 detents (straight and triplet values) | 0x0801307c → 0x0800e42c |
| 5 | PC4 / 14 | Aftertouch sensor → channel pressure (0xD0) | 0x0800d1dc |

ADC init 0x080092a0, handle `0x200004D8`, sampler object `0x2000114C`.

Both detent knobs share one decoder (0x080118e2). The raw reading is matched against fixed windows,
which are not evenly spaced:

| Position | Raw window |
|---|---|
| 7 | 0x173–0x239 |
| 6 | 0x317–0x3DD |
| 5 | 0x4AA–0x570 |
| 4 | 0x647–0x70D |
| 3 | 0x808–0x8CE |
| 2 | 0xA0B–0xAD1 |
| 1 | 0xC7B–0xD41 |
| 0 | ≥ 0xF9C |
| 8 (none) | anything else |

A position counts only after 18 equal readings in a row (0x08011a04). Position 8 is ignored by
both knob handlers. The time-division position is used directly as the division index
(sequencer.md 2.2); the mode-knob position is the sequence or arp mode index.

### Touch strips (I2C)

Two identical capacitive touch controllers, 8-bit I2C address `0x2A` (7-bit `0x15`), one per bus,
100 kHz. Bus is recovered with 64 SCL pulses on failure (0x08011250).

Set-up (0x0800d184 = init 0x0800d020 + verify 0x0800cbdc + enable), single-byte register writes,
values from the table at `0x08019D60`:

| Step | Register ← value |
|---|---|
| 1 | 0xAF ← 0x3A, 0x1F ← 0x00 |
| 2 | 0x00..0x0F ← 0x00..0x0F (identity), 0x10 ← 0x06 |
| 3 | 0xE0..0xF2 ← 0x00 |
| 4 | 0x70..0xA8 ← 0x00 |
| 5 | 0xF3 ← 0x55 |
| 6 (verify) | 0x11 ← 0x00, 0x12 ← 0x08, 0x13 ← 0x00, 0x14 ← 0x3F, 0x15 ← 0, 0x16 ← 0, 0x1D ← 0, 0x1E ← 0x7F |
| 7 (enable) | 0xAF ← 0x3A, 0x1F ← 0x01 |

Writing 0x1F ← 0x10 and polling until it reads 0 is an optional calibrate step.

Position: one byte from register 0x21, read by an 8-step non-blocking state machine
(0x0800ca82, one step per main-loop pass); raw 0 = not touched. Scaling (0x08011192):
`s = clamp(((raw − lo)·0x7F0 / (hi − lo)) >> 4, 0, 127)` with calibration lo/hi
(`0x0803F200`: byte 0 → hi, byte 1 → lo; defaults hi 0xBF, lo 0x09). Strip behaviour is in
[keyboard.md](keyboard.md) §9–10.

| Bus | Pins | Function | Consumer |
|---|---|---|---|
| I2C1 | PB6 SCL / PB7 SDA | Pitch-bend strip (dead zone 0x37–0x4A) | 0x0800be40 |
| I2C2 | PB10 SCL / PB11 SDA | Mod strip → CC (settings+0x40, default 1) and Mod CV | 0x0800c0f0 |

### CV / Gate outputs

| Output | Hardware | Evidence |
|---|---|---|
| Pitch CV | TIM3 full remap, 8-bit PWM: **PC6 = CH1 high byte, PC7 = CH2 low byte** (summed externally → 16-bit). 533 codes/semitone, V/oct or Hz/V, no per-unit calibration — see [pitch_cv.md](pitch_cv.md) | `pitch_cv_write16` 0x08013154, note→CV 0x08018d9c |
| Mod CV | DAC ch1 (PA4), 12-bit, scaled by max-voltage setting (settings+0x7B, 5–12 V) | 0x08013256, 0x08016a20 |
| Mod CV source | settings+0x7A: 0 mod strip, 1 velocity, 2 aftertouch | 0x0800c0f0, 0x08016930, 0x0800d1dc |
| Gate | PA1, output inverted (transistor driver); logic polarity flipped in mode 0 (S-trig) | `gate_write` 0x0801345c |
| Gate type | settings+0x73, PA2/PA0: 0 S-trig → 1/1, 1 V-trig 5 V → 1/0, 2 V-trig 12 V → 0/1 (names from manual order + polarity flip) | 0x08013374 |

Note: the KeyFreak author reports PA5 behaves as Mod CV. The stock firmware only configures DAC
channel 1 (PA4); verify on hardware.

### Sync (3.5 mm jacks)

| Function | Pin | Notes |
|---|---|---|
| Sync in clock | PD0 | EXTI0 both edges, handler 0x0800455c |
| Sync in start/stop / detect | PD3 | EXTI3 both edges, handler 0x08004738 |
| Sync out clock | PB8 | |
| Sync out start/stop | PD2 | |

All four lines are active low at the GPIO. At boot and on stop both outputs are driven high; on
start the start/stop output goes low (transport handler 0x0800e01c, init 0x080150b0). The clock input handler acts
when the pin reads 0 after an edge, and the start/stop input means Play when it reads 0 and Stop
when it reads 1 (EXTI3 handler). The thru path copies each input pin level to its output pin.

Sync object `0x20001134` (ctor 0x080150e6). Clock type from settings+0x4C (0x0800e96c):
0 1 step Gate, 1 1 step Clock (default), 2 Korg, 3 24 PPQ, 4 48 PPQ, i.e. 20 / 120 / 240 / 20 / 10
ticks of the 480 PPQN clock per pulse (see [sequencer.md](sequencer.md) §4 and
[settings.md](settings.md)). Sync start mode settings+0x4F: On Clock / On Gate Start (TRS ring).

### LEDs

8 LEDs on a serial-in shift register: SPI2 (PB13 SCK, PB15 MOSI, master, /16, 1 byte),
latch **PB12**, PB14 held high at init (shift register reset/OE (?)). Global brightness = PWM on
**PC9 (TIM3_CH4)** driving the shift register OE (settings+0x97, 10–100 %). PWM mode 1 with a
0..255 period; CCR = percent × 255 / 100, so more high time is brighter (`leds_set_brightness_pct` 0x08013b12).
The shift-register bits are active low: 0 lights the LED. Evidence: the octave LED object is
built with LEDs 3 (Oct+) and 4 (Oct−) (0x08018d8a), and at octave 0 it writes 1 to both, so both
are dark. Shift+Oct− (Transpose) writes 0 to bit 4 and 1 to bit 3, and the manual says Oct− lights
and Oct+ goes dark. Bit 6 (Tap) lights on transport start and blinks with the beat (lit for the first 12 MIDI clocks).
Bit 5 (Rec) lights when recording starts, follows the steps while recording, and goes dark when
the mode switch selects Arp. The LED numbers come from `button_ctor` (id, led): Tap is built with
LED 6; the Record button passes no LED and its handler drives bit 5 directly.
Driver object `0x20000F88`: ctor 0x08013dd8, set bit 0x08013ae8, flush 0x08013da4, blink scheduler 0x08013b54.

**Brightness fades** (0x08013c1c starts one, 0x08013968 steps it). A countdown at `0x2000004E` is
decremented once per ms by SysTick (every 10th tick). At boot the level ramps up from 0: to the
full level at 10 ms per percent, or to the low level at 20 ms per percent. When PD4 changes, the
level fades between the low and full levels at 10 ms per percent. A brightness setting change
while no fade is running applies at once, never below 1 %.

**Power-up light show** (`leds_boot_animation` 0x0801379c, blocking). The LEDs are lit one at a
time in the order Play, Stop, Rec, Tap, Hold, Oct−, Oct+, Shift (table at 0x08019d58). Each fades
0→30 % over 60 ms and back to 0 over 60 ms. Then all LEDs fade 0→20→0 % over 400 ms, and all go
dark. A second run happens before the factory reset (Oct− + Oct+ held, or blank sequence flash):
it walks Hold, Oct−, Oct+, Shift twice.

### MIDI / USB

| Item | Value |
|---|---|
| DIN MIDI | USART1 PA9 TX / PA10 RX, 31250 baud, IRQ 0x0800a440 (RX → parser 0x08012906) |
| USB | FS device, MIDI class, EP 0x81 IN / 0x02 OUT, D+ pull-up switched by PA8 (high = connected) |
| Output router | singleton `0x20000258` (0x08019b0c); `midi_out_usb` 0x080169c4 (ring of 36), `midi_out_din` 0x080166c8 |

### Unused / analog-parked pins

PA3, PA15, PB0, PB1, PB3, PB4, PB9, PC5, PC10–PC13, PD1, PD5–PD7, PE0, PE1, PE5, PE6.
PC8 and PB5 are configured as EXTI events (rising) without a handler — purpose unknown.
PC14/PC15 outputs cleared at boot (?).

## Settings object (`0x20005058`)

| Offset | Meaning |
|---|---|
| +0x04..+0x23 | listener list (≤ 8), count at +0x24 |
| +0x40 | mod strip CC number |
| +0x42 | MIDI channel (copied to 0x20000052) |
| +0x4C | sync clock type (see Sync) |
| +0x4F | sync start: On Clock / On Gate Start |
| +0x58..+0x5C | booleans, range-checked in 0x080187b4 |
| +0x72 | pitch CV mode: 0 V/oct, 1 Hz/V |
| +0x76 | V/oct 0 V note (default 0) |
| +0x78 | Hz/V reference note (default 24) |
| +0x7C | pitch-bend range, 1–24 semitones (default 2) |
| +0x73 | gate type: 0 S-trig, 1 V-trig 5 V, 2 V-trig 12 V |
| +0x7A | mod CV source |
| +0x7B | mod CV max voltage, 5–12 V |
| +0x97 | LED brightness % |
| +0xA9 | factory-test mode flag |

Settings change events are broadcast as `{1, code}` with codes like `'G'` (gate), `'O'` (mod CV range),
`'F'`, `'J'`, `'L'`, `'P'`, `'%'`, `'k'`.
