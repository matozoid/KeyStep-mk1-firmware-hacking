#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include "stm32f103.h"

#define SYSCLK_HZ 72000000u
#define APB1_HZ   36000000u
#define APB2_HZ   72000000u

void system_init(void);
uint32_t millis(void);
uint32_t micros(void);
void delay_ms(uint32_t ms);
void system_reset(void);

void gpio_config(GPIO_t *port, unsigned pin, uint32_t mode);
void gpio_write(GPIO_t *port, unsigned pin, int level);
int gpio_read(GPIO_t *port, unsigned pin);

#endif
