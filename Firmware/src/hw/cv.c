/* CV/Gate outputs.
 *
 * Pitch CV: TIM3 full remap, 8-bit PWM, PC6 = CH1 high byte, PC7 = CH2 low byte.
 * LED brightness: TIM3_CH4 on PC9 (shift-register output enable).
 * Mod CV: DAC channel 1 (PA4), 12-bit.
 * Gate: PA1 drives an inverting transistor; PA2/PA0 select the gate type. */
#include "cv.h"
#include "system.h"

static uint8_t gate_type;

void cv_init(void)
{
    RCC->APB1ENR |= RCC_APB1_TIM3 | RCC_APB1_DAC;

    AFIO->MAPR = (AFIO->MAPR & ~(3u << 10)) | (3u << 10); /* TIM3 full remap -> PC6..PC9 */
    gpio_config(GPIOC, 6, GPIO_AF_PP);
    gpio_config(GPIOC, 7, GPIO_AF_PP);
    gpio_config(GPIOC, 9, GPIO_AF_PP);

    TIM3->PSC = 0;
    TIM3->ARR = 0xFF;                            /* 72 MHz / 256 = 281 kHz PWM */
    TIM3->CCMR1 = (6u << 4) | (1u << 3)          /* CH1 PWM mode 1, preload */
                | (6u << 12) | (1u << 11);       /* CH2 */
    TIM3->CCMR2 = (6u << 12) | (1u << 11);       /* CH4 */
    TIM3->CCER = (1u << 0) | (1u << 4) | (1u << 12);
    TIM3->CCR1 = 0;
    TIM3->CCR2 = 0;
    TIM3->CCR4 = 0xFF;
    TIM3->EGR = 1;
    TIM3->CR1 = (1u << 7) | 1u;                  /* ARPE, enable */

    gpio_config(GPIOA, 4, GPIO_ANALOG);
    DAC->CR = 1u;                                /* channel 1 on, buffer on, no trigger */
    DAC->DHR12R1 = 0;

    gpio_config(GPIOA, 0, GPIO_OUT_PP);
    gpio_config(GPIOA, 1, GPIO_OUT_PP);
    gpio_config(GPIOA, 2, GPIO_OUT_PP);
    cv_set_gate_type(GATE_VTRIG_5V);
    cv_gate(0);
}

void cv_pitch(uint16_t code)
{
    TIM3->CCR1 = code >> 8;
    TIM3->CCR2 = code & 0xFFu;
}

void cv_mod(uint16_t value12)
{
    DAC->DHR12R1 = value12 & 0xFFFu;
}

void cv_set_gate_type(enum gate_type type)
{
    /* PA2/PA0: S-trig 1/1, V-trig 5 V 1/0, V-trig 12 V 0/1 */
    static const uint8_t pa2[3] = {1, 1, 0}, pa0[3] = {1, 0, 1};
    if (type > GATE_VTRIG_12V)
        type = GATE_VTRIG_5V;
    gate_type = (uint8_t)type;
    gpio_write(GPIOA, 2, pa2[type]);
    gpio_write(GPIOA, 0, pa0[type]);
}

void cv_gate(int on)
{
    /* The driver inverts; S-trig additionally flips the logic (output pulled low while on). */
    int level = gate_type == GATE_STRIG ? on : !on;
    gpio_write(GPIOA, 1, level);
}

void cv_led_brightness(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    /* Stock: PWM mode 1 on a 0..255 period, CCR = percent * 255 / 100, so a longer high time
     * is brighter (leds_set_brightness_pct). */
    TIM3->CCR4 = (255u * percent) / 100u;
}
