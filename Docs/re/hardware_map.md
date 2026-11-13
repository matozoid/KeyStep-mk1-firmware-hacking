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
| SysTick | 1 kHz software timers, LED blink | 0x08009d44 |
| Watchdog | IWDG, reload 0xFFF | 0x08009600 |

## Flash layout used by the app

| Address | Size | Content | Evidence |
|---|---|---|---|
| `0x0803B000 + n*0x800`, n=0..7 | 8 × 2 KB | 8 sequences (RAM copies at `0x200022C8 + n*0x408`) | `seq_flash_is_empty` 0x08015c08, `seq_flash_erase_all` 0x08015bd0 |
| `0x0803F000` | 0x80 bytes | global settings → RAM `0x20005084` | 0x08015cd0 |
| `0x0803F200` | 4 bytes | touch-strip calibration (min/max for pitch & mod strip; default 0xBF/0x09) | 0x08015eb8 |

Holding both octave buttons at power-on erases the sequence pages (`main` 0x0800c1f4).

## Pins

### Keybed (32 keys, 2 contacts each → velocity)

| Function | Pins | Notes |
|---|---|---|
| Matrix select (outputs) | PE8–PE15 | one-hot, 8 lines |
| Matrix read (inputs) | PD8–PD15 | read `GPIOD->IDR >> 8`, inverted (active low) |

Object `KeyMatrix` @ `0x20000F44`, ctor 0x08016610, select 0x080165ea, read 0x080165b8.
Velocity = time between the two contacts; three 6-segment curves (lin/log/exp)
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
| 0 | 0x2000129C | 2 | Hold (?) — LED 2 is also driven by the sustain/hold input |
| 1 | 0x200012D0 | 7 | Shift (sets "shift held" flag 0x2000005F; suppresses knobs) |
| 2 | 0x20001304 | 4 | Octave − (?) (flag 0x2000005E) |
| 3 | 0x20001338 | — | Octave + (?) (flag 0x2000005D) |
| 4 | 0x2000136C | 6 | Record |
| 5 | 0x200013A0 | — | Tap / Chord (?) |
| 6 | 0x200013D4 | 0 | Stop (3 presses → CC 123 All Notes Off on all 16 channels) |
| 7 | 0x20001408 | 1 | Play |

### Switches

| Function | Pins | Evidence |
|---|---|---|
| Sync source selector (2-bit) | PE2, PE3 (2-bit, inverted) | 0x08018a46, 0x08018994 → clock `set_source` 0x0800e5d0 |
| Seq / Arp mode switch | PE4 | 0x08011cb4 → `ui_set_seq_arp` 0x08014070 |
| PD4 (?) | PD4 | 0x0800f20c, only changes LED fade mode — power/USB detect (?) |

### Analog inputs (ADC1, 5-channel scan, DMA1 ch1)

| Rank | Pin / ch | Function | Evidence |
|---|---|---|---|
| 1 | PC0 / 10 | Tempo knob → 30.00–240.00 BPM | 0x080134d8, 0x0801360c |
| 2 | PC1 / 11 | Sustain/Hold pedal (digital with hysteresis, polarity sampled at boot) | 0x08011a88, 0x08011b04 |
| 3 | PC2 / 12 | Mode knob, 8 detents (arp mode / sequence 1–8) | 0x08011da4 |
| 4 | PC3 / 13 | Time-division knob, 8 detents | 0x0801307c → 0x0800e42c |
| 5 | PC4 / 14 | Aftertouch sensor → channel pressure (0xD0) | 0x0800d1dc |

ADC init 0x080092a0, handle `0x200004D8`, sampler object `0x2000114C`.

### Touch strips (I2C)

Two identical capacitive touch controllers, 8-bit I2C address `0x2A` (7-bit `0x15`), one per bus,
100 kHz. Init writes registers 0xAF, 0x1F, 0x00–0x10, 0xE0–0xF2, 0x70–0xA8, 0xF3 from table
`0x08019D60` (0x0800d020 / 0x0800cbdc); position read 0x080111ec; scaled 0..127 via calibration
(0x08011192). Bus is recovered with 64 SCL pulses on failure (0x08011250).

| Bus | Pins | Function | Consumer |
|---|---|---|---|
| I2C1 | PB6 SCL / PB7 SDA | Pitch-bend strip (dead zone 0x37–0x4A) | 0x0800be40 |
| I2C2 | PB10 SCL / PB11 SDA | Mod strip → CC (settings+0x40, default 1) and Mod CV | 0x0800c0f0 |

### CV / Gate outputs

| Output | Hardware | Evidence |
|---|---|---|
| Pitch CV | TIM3 full remap, 8-bit PWM: **PC6 = CH1 high byte, PC7 = CH2 low byte** (summed externally → 16-bit). Value from calibration table, clamped to 0x1086B | `pitch_cv_write16` 0x08013154, note→CV 0x08018d9c |
| Mod CV | DAC ch1 (PA4), 12-bit, scaled by max-voltage setting (settings+0x7B, 5–12 V) | 0x08013256, 0x08016a20 |
| Mod CV source | settings+0x7A: 0 mod strip, 1 velocity, 2 aftertouch | 0x0800c0f0, 0x08016930, 0x0800d1dc |
| Gate | PA1, output inverted (transistor driver) | `gate_write` 0x0801345c |
| Gate level select | PA2/PA0: mode 0 → 1/1, 1 → 1/0, 2 → 0/1 (settings+0x73) | 0x08013374 |

Note: the KeyFreak author reports PA5 behaves as Mod CV. The stock firmware only configures DAC
channel 1 (PA4); verify on hardware.

### Sync (3.5 mm jacks)

| Function | Pin | Notes |
|---|---|---|
| Sync in clock | PD0 | EXTI0 both edges, handler 0x0800455c |
| Sync in start/stop / detect | PD3 | EXTI3 both edges, handler 0x08004738 |
| Sync out clock | PB8 | |
| Sync out start/stop | PD2 | |

Sync object `0x20001134` (ctor 0x080150e6). Out PPQN from settings+0x4C:
cases 0–4 → 48 / 8 / 4 / 48 / 96 (0x0800e96c).

### LEDs

8 LEDs on a serial-in shift register: SPI2 (PB13 SCK, PB15 MOSI, master, /16, 1 byte),
latch **PB12**, PB14 held high at init (shift register reset/OE (?)). Global brightness = PWM on
**PC9 (TIM3_CH4)** driving the shift register OE (settings+0x97, 10–100 %).
Driver object `0x20000F88`: ctor 0x08013dd8, set bit 0x08013ae8, flush 0x08013da4, blink scheduler 0x08013b54.

### MIDI / USB

| Item | Value |
|---|---|
| DIN MIDI | USART1 PA9 TX / PA10 RX, 31250 baud, IRQ 0x0800a440 (RX → parser 0x08012906) |
| USB | FS device, MIDI class, EP 0x81 IN / 0x02 OUT, D+ pull-up switched by PA8 |
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
| +0x4C | sync out rate index |
| +0x4F | sync in start/stop mode |
| +0x58..+0x5C | booleans, range-checked in 0x080187b4 |
| +0x72/+0x76/+0x78/+0x7C | velocity curve settings (?) (0x08018f9c) |
| +0x73 | gate level mode (0–2) |
| +0x7A | mod CV source |
| +0x7B | mod CV max voltage (V) |
| +0x97 | LED brightness % |
| +0xA9 | factory-test mode flag |

Settings change events are broadcast as `{1, code}` with codes like `'G'` (gate), `'O'` (mod CV range),
`'F'`, `'J'`, `'L'`, `'P'`, `'%'`, `'k'`.
