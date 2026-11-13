# Keyboard matrix

## Logical pinout

```text
Select lines: PE8, PE9, PE10, PE11, PE12, PE13, PE14, PE15
Read lines:   PD8, PD9, PD10, PD11, PD12, PD13, PD14, PD15
Logic: active-low
```

## Basic scanning

```c
static uint8_t scan_row(uint8_t row)
{
    GPIOE->ODR |= 0xFF00;              // all inactive HIGH
    GPIOE->ODR &= ~(1u << (8 + row));  // one row active LOW

    for (volatile int i = 0; i < 80; i++) {}

    return (uint8_t)(~(GPIOD->IDR >> 8) & 0xFF);
}
```

## Contact mapping

```text
Key group 0 (8 keys):
  contact A → PE8  reads PD8–PD15
  contact B → PE9  reads PD8–PD15

Key group 1 (8 keys):
  contact A → PE10 reads PD8–PD15
  contact B → PE11 reads PD8–PD15

Key group 2 (8 keys):
  contact A → PE12 reads PD8–PD15
  contact B → PE13 reads PD8–PD15

Key group 3 (8 keys):
  contact A → PE14 reads PD8–PD15
  contact B → PE15 reads PD8–PD15
```

For `keyIndex` from 0 to 31:

```text
group  = keyIndex / 8
column = keyIndex % 8

Contact A:
  select = PE(8 + group*2)
  read   = PD(8 + column)

Contact B:
  select = PE(8 + group*2 + 1)
  read   = PD(8 + column)
```
