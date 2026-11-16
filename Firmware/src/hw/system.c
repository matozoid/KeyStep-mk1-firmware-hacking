/* Clock tree, SysTick time base and GPIO helpers. */
#include "system.h"
#include "stm32f103.h"

#define APP_BASE 0x08004000u

static volatile uint32_t ms_ticks;

void system_init(void)
{
    SCB_VTOR = APP_BASE;

    /* The bootloader may leave the PLL running: fall back to HSI before reprogramming it. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }
    RCC->CFGR &= ~3u; /* SW = HSI */
    while (RCC->CFGR & (3u << 2)) {
    }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {
    }

    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
    }

    FLASH->ACR = (1u << 4) | 2u; /* prefetch on, 2 wait states for 72 MHz */

    /* PLL = HSE 8 MHz x 9 = 72 MHz. AHB /1, APB1 /2 (36 MHz), APB2 /1, ADC /6 (12 MHz),
     * USB = PLL / 1.5 = 48 MHz (USBPRE = 0). */
    RCC->CFGR = (7u << 18)   /* PLLMUL x9 */
              | (1u << 16)   /* PLLSRC = HSE */
              | (2u << 14)   /* ADCPRE /6 */
              | (4u << 8);   /* PPRE1 /2 */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
    }
    RCC->CFGR |= 2u; /* SW = PLL */
    while (((RCC->CFGR >> 2) & 3u) != 2u) {
    }

    RCC->AHBENR |= RCC_AHB_DMA1;
    RCC->APB2ENR |= RCC_APB2_AFIO | RCC_APB2_IOPA | RCC_APB2_IOPB | RCC_APB2_IOPC | RCC_APB2_IOPD |
                    RCC_APB2_IOPE;

    /* JTAG off, SWD on: frees PA15, PB3, PB4 (unused by the board). */
    AFIO->MAPR = (AFIO->MAPR & ~(7u << 24)) | (2u << 24);

    SYSTICK->LOAD = SYSCLK_HZ / 1000u - 1u;
    SYSTICK->VAL = 0;
    SYSTICK->CTRL = 7u; /* core clock, interrupt, enable */
}

void SysTick_Handler(void)
{
    ms_ticks++;
}

uint32_t millis(void)
{
    return ms_ticks;
}

uint32_t micros(void)
{
    uint32_t ms, val;
    do {
        ms = ms_ticks;
        val = SYSTICK->VAL;
    } while (ms != ms_ticks);
    return ms * 1000u + (SYSCLK_HZ / 1000u - 1u - val) / (SYSCLK_HZ / 1000000u);
}

void delay_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;
    while (ms_ticks - start < ms) {
    }
}

void gpio_config(GPIO_t *port, unsigned pin, uint32_t mode)
{
    volatile uint32_t *cr = pin < 8 ? &port->CRL : &port->CRH;
    unsigned shift = (pin & 7u) * 4u;
    *cr = (*cr & ~(0xFu << shift)) | (mode << shift);
}

void gpio_write(GPIO_t *port, unsigned pin, int level)
{
    port->BSRR = level ? (1u << pin) : (1u << (pin + 16));
}

int gpio_read(GPIO_t *port, unsigned pin)
{
    return (port->IDR >> pin) & 1u;
}

void system_reset(void)
{
    SCB_AIRCR = (0x5FAu << 16) | (1u << 2);
    for (;;) {
    }
}
