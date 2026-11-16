#include "sync.h"
#include "system.h"

/* Pin levels as the stock firmware uses them (hardware_map.md, Sync): all four lines are
 * active low at the GPIO. Outputs idle high, a clock pulse and "running" are low; the inputs
 * count a clock on the falling pin edge and read "running" as low. The stock thru path copies
 * the input pin level straight to the output pin, so in and out share one convention. */
#define SYNC_IN_CLOCK_RISING 0
#define SYNC_IN_RUN_HIGH     0
#define SYNC_OUT_HIGH        0

static volatile unsigned pulses;

void EXTI0_IRQHandler(void)
{
    EXTI->PR = 1u << 0;
    pulses++;
}

void EXTI3_IRQHandler(void)
{
    EXTI->PR = 1u << 3; /* start/stop is sampled by level in sync_in_run() */
}

void sync_init(void)
{
    gpio_config(GPIOD, 0, GPIO_IN_FLOATING);
    gpio_config(GPIOD, 3, GPIO_IN_FLOATING);
    gpio_config(GPIOB, 8, GPIO_OUT_PP);
    gpio_config(GPIOD, 2, GPIO_OUT_PP);
    sync_out(0, 0);

    /* EXTI0 <- port D (AFIO_EXTICR1 field 0, value 3) */
    AFIO->EXTICR[0] = (AFIO->EXTICR[0] & ~0xFu) | 0x3u;
    if (SYNC_IN_CLOCK_RISING)
        EXTI->RTSR |= 1u << 0;
    else
        EXTI->FTSR |= 1u << 0;
    EXTI->PR = 1u << 0;
    EXTI->IMR |= 1u << 0;
    nvic_priority(IRQ_EXTI0, 1);
    nvic_enable(IRQ_EXTI0);
}

unsigned sync_in_take_pulses(void)
{
    uint32_t s = irq_save();
    unsigned n = pulses;
    pulses = 0;
    irq_restore(s);
    return n;
}

int sync_in_run(void)
{
    return gpio_read(GPIOD, 3) == SYNC_IN_RUN_HIGH;
}

void sync_out(int clock, int run)
{
    gpio_write(GPIOB, 8, clock ? SYNC_OUT_HIGH : !SYNC_OUT_HIGH);
    gpio_write(GPIOD, 2, run ? SYNC_OUT_HIGH : !SYNC_OUT_HIGH);
}

int sync_loop_test(int line)
{
    GPIO_t *out_port = line ? GPIOD : GPIOB, *in_port = GPIOD;
    unsigned out_pin = line ? 2u : 8u, in_pin = line ? 3u : 0u;
    int ok = 1;
    for (int i = 0; i < 5 && ok; i++) {
        for (int level = 0; level <= 1 && ok; level++) {
            gpio_write(out_port, out_pin, level);
            uint32_t t = micros();
            while (micros() - t < 100u) {
            }
            ok = gpio_read(in_port, in_pin) == level;
        }
    }
    sync_out(0, 0);
    (void)sync_in_take_pulses(); /* the test edges are not clock pulses */
    return ok;
}
