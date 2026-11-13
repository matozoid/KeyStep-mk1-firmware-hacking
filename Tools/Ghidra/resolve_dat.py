#!/usr/bin/env python3
"""Replace DAT_08xxxxxx literal-pool references in decompiled C with their 32-bit value.

Usage: resolve_dat.py image.bin [base=0x08004000] < in.c > out.c
Peripheral values get a symbolic suffix, e.g. DAT_08009574 -> 0x40010800/*GPIOA*/.
"""
import re
import struct
import sys

PERIPH = {
    0x40000000: "TIM2", 0x40000400: "TIM3", 0x40000800: "TIM4", 0x40000C00: "TIM5",
    0x40003000: "IWDG", 0x40003800: "SPI2", 0x40004400: "USART2", 0x40004800: "USART3",
    0x40005400: "I2C1", 0x40005800: "I2C2", 0x40005C00: "USB", 0x40007000: "PWR",
    0x40007400: "DAC", 0x40010000: "AFIO", 0x40010400: "EXTI", 0x40010800: "GPIOA",
    0x40010C00: "GPIOB", 0x40011000: "GPIOC", 0x40011400: "GPIOD", 0x40011800: "GPIOE",
    0x40012400: "ADC1", 0x40012800: "ADC2", 0x40012C00: "TIM1", 0x40013000: "SPI1",
    0x40013400: "TIM8", 0x40013800: "USART1", 0x40020000: "DMA1", 0x40020400: "DMA2",
    0x40021000: "RCC", 0x40022000: "FLASH",
}


def name(v: int) -> str:
    base = v & ~0x3FF
    if base in PERIPH:
        off = v - base
        return f"0x{v:08X}/*{PERIPH[base]}{'+0x%X' % off if off else ''}*/"
    return f"0x{v:X}"


def main() -> None:
    img = open(sys.argv[1], "rb").read()
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x08004000

    def sub(m: re.Match) -> str:
        a = int(m.group(1), 16) - base
        if 0 <= a <= len(img) - 4:
            return name(struct.unpack_from("<I", img, a)[0])
        return m.group(0)

    sys.stdout.write(re.sub(r"DAT_(08[0-9a-f]{6})\b", sub, sys.stdin.read()))


if __name__ == "__main__":
    main()
