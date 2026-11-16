# Keyboard matrix

32 keys, two contacts each (A closes first, B closes at the bottom of the travel). Velocity comes
from the time between A and B: short = hard, long = soft. The stock firmware maps that time through
one of three curves (linear, logarithmic, exponential).

## Pins

```text
Select lines: PE8–PE15, outputs, one driven low at a time
Read lines:   PD8–PD15, inputs, active low
```

The buttons share PD8–PD11 as their read lines (selected by PE7 / PB2 instead), so the keybed select
lines must all be high while buttons are scanned, and the other way round.

## Scanning

```c
static uint8_t scan_row(uint8_t row)
{
    GPIOE->ODR |= 0xFF00;              // all select lines high
    GPIOE->ODR &= ~(1u << (8 + row));  // select one row (low)

    for (volatile int i = 0; i < 80; i++) {}   // settle

    return (uint8_t)(~(GPIOD->IDR >> 8) & 0xFF);
}
```

## Contact mapping

For `key` 0–31 (0 = highest key, the top C; 31 = lowest key, the bottom F; confirmed on hardware):

```text
group  = key / 8
column = key % 8

Contact A: select PE(8 + group*2),     read PD(8 + column)
Contact B: select PE(8 + group*2 + 1), read PD(8 + column)
```

Example: key 0 = PE8/PD8 (A), PE9/PD8 (B). Key 31 = PE14/PD15 (A), PE15/PD15 (B).
