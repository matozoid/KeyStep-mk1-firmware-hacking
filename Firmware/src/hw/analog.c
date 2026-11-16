/* ADC1 continuous scan of the 5 panel analog inputs into RAM by DMA1 channel 1.
 * Rank order: PC0 tempo, PC1 pedal, PC2 mode knob, PC3 time division, PC4 aftertouch. */
#include "analog.h"
#include "system.h"

static volatile uint16_t samples[ANALOG_COUNT];

void analog_init(void)
{
    RCC->APB2ENR |= RCC_APB2_ADC1;
    for (unsigned p = 0; p <= 4; p++)
        gpio_config(GPIOC, p, GPIO_ANALOG);

    DMA_Channel_t *ch = &DMA1->CH[0];
    ch->CCR = 0;
    ch->CPAR = (uint32_t)&ADC1->DR;
    ch->CMAR = (uint32_t)samples;
    ch->CNDTR = ANALOG_COUNT;
    /* 16-bit both sides, memory increment, circular, enable */
    ch->CCR = (1u << 10) | (1u << 8) | (1u << 7) | (1u << 5) | 1u;

    ADC1->CR1 = 1u << 8; /* SCAN */
    ADC1->SMPR1 = 0;
    for (unsigned c = 10; c <= 14; c++)
        ADC1->SMPR1 |= 7u << ((c - 10) * 3); /* 239.5 cycles: high-impedance pots */
    ADC1->SQR1 = (ANALOG_COUNT - 1u) << 20;
    ADC1->SQR3 = 10u | (11u << 5) | (12u << 10) | (13u << 15) | (14u << 20);
    /* DMA, continuous, software-triggered (EXTSEL = SWSTART, EXTTRIG), power on */
    ADC1->CR2 = (1u << 8) | (1u << 1) | (7u << 17) | (1u << 20) | 1u;
    for (volatile int i = 0; i < 100; i++) {
    }
    ADC1->CR2 |= 1u << 3; /* reset calibration */
    while (ADC1->CR2 & (1u << 3)) {
    }
    ADC1->CR2 |= 1u << 2; /* calibrate */
    while (ADC1->CR2 & (1u << 2)) {
    }
    ADC1->CR2 |= 1u << 22; /* SWSTART */
}

uint16_t analog_read(enum analog_input in)
{
    return samples[in];
}
