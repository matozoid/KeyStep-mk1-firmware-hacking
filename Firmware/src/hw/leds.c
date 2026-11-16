/* 8 panel LEDs on a serial-in shift register: SPI2 (PB13 SCK, PB15 MOSI), latch PB12,
 * PB14 held high. Brightness PWM is in cv.c (TIM3_CH4). */
#include "leds.h"
#include "system.h"

static uint8_t shown = 0xFFu;

void leds_init(void)
{
    RCC->APB1ENR |= RCC_APB1_SPI2;
    gpio_config(GPIOB, 13, GPIO_AF_PP);
    gpio_config(GPIOB, 15, GPIO_AF_PP);
    gpio_config(GPIOB, 12, GPIO_OUT_PP);
    gpio_config(GPIOB, 14, GPIO_OUT_PP);
    gpio_write(GPIOB, 12, 0);
    gpio_write(GPIOB, 14, 1);

    /* master, /16 (2.25 MHz), CPOL 0 / CPHA 0, MSB first, software NSS */
    SPI2->CR1 = (1u << 2) | (3u << 3) | (1u << 8) | (1u << 9);
    SPI2->CR1 |= 1u << 6;
    leds_write(0);
}

void leds_write(uint8_t bits)
{
    if (bits == shown)
        return;
    shown = bits;
    while (!(SPI2->SR & SPI_SR_TXE)) {
    }
    SPI2->DR = (uint8_t)~bits; /* the register outputs sink the LEDs: 0 = lit (hardware_map.md) */
    /* BSY rises a few cycles after the DR write, so wait for the end of the transfer through
     * RXNE first. Latching early shows the previous byte shifted by one bit. */
    while (!(SPI2->SR & SPI_SR_RXNE)) {
    }
    (void)SPI2->DR;
    while (SPI2->SR & SPI_SR_BSY) {
    }
    gpio_write(GPIOB, 12, 1);
    gpio_write(GPIOB, 12, 0);
}
