/* DIN MIDI on USART1: PA9 TX, PA10 RX, 31250 baud, interrupt driven both ways. */
#include "midi_uart.h"
#include "system.h"
#include "../core/ringbuf.h"

static ringbuf_t rx, tx;

void midi_uart_init(void)
{
    ringbuf_init(&rx);
    ringbuf_init(&tx);
    RCC->APB2ENR |= RCC_APB2_USART1;
    gpio_config(GPIOA, 9, GPIO_AF_PP);
    gpio_config(GPIOA, 10, GPIO_IN_PULL);
    gpio_write(GPIOA, 10, 1);

    USART1->BRR = APB2_HZ / 31250u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    nvic_priority(IRQ_USART1, 1);
    nvic_enable(IRQ_USART1);
}

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE))
        ringbuf_put(&rx, (uint8_t)USART1->DR); /* reading DR also clears ORE */
    if ((USART1->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        uint8_t b;
        if (ringbuf_get(&tx, &b))
            USART1->DR = b;
        else
            USART1->CR1 &= ~USART_CR1_TXEIE;
    }
}

int midi_uart_send(const uint8_t *data, unsigned len)
{
    if (ringbuf_space(&tx) < len)
        return 0;
    for (unsigned i = 0; i < len; i++)
        ringbuf_put(&tx, data[i]);
    USART1->CR1 |= USART_CR1_TXEIE;
    return 1;
}

int midi_uart_read(uint8_t *b)
{
    return ringbuf_get(&rx, b);
}
