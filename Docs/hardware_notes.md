# Hardware notes

## Device

- Arturia KeyStep Mk1 / KeyStep 32
- MCU observed on the board: `STM32F103VCT6`
- Architecture: ARM Cortex-M3
- Expected flash/RAM for this variant: 256 KB flash / 48 KB RAM

## Application base address

During analysis of the official firmware, the application was treated as based at:

```text
0x08004000
```

## Relevant DAC outputs

```text
PA4 = DAC_OUT1 / DAC1 / Pitch CV
PA5 = DAC_OUT2 / DAC2 / Mod CV
```

On the tested unit:

```text
Mod CV / PA5 / DAC2 = working audio output
Pitch CV / PA4 / DAC1 = did not work as usable audio
```

## Keyboard matrix

The real matrix was identified from Ghidra:

```c
InputScanner8_Init(DAT_080196e4, DAT_080196e0, 8, DAT_080196dc, 8);
```

With:

```text
DAT_080196e4 = 0x20000F44   scanner object
DAT_080196e0 = 0x40011800   GPIOE
DAT_080196dc = 0x40011400   GPIOD
```

Therefore:

```text
PE8–PE15 = select, active-low
PD8–PD15 = read, active-low
```

This gives:

```text
8 selects × 8 reads = 64 contacts
32 keys × 2 contacts = 64 contacts
```
