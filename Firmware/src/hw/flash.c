/* Internal flash page erase/program (2 KB pages on the STM32F103VC). Only the data area
 * 0x0803B000-0x0803FFFF may be written; the bootloader and the running image are refused.
 *
 * While the flash is busy every read from it stalls, so code in flash stops and no interrupt
 * handler can start. A half-word program takes about 50 us, which is harmless. A page erase takes
 * 20-40 ms: the wait for it runs from RAM and does the two jobs that cannot wait that long, counting
 * SysTick periods and reading the DIN MIDI input. */
#include "flash.h"
#include "system.h"
#include "midi_uart.h"

#define ERASE_RX_MAX 160u /* 40 ms at 31250 baud is 125 bytes */

typedef struct {
    uint32_t ticks; /* SysTick periods that ended during the erase */
    uint32_t rx_n;
    uint8_t rx[ERASE_RX_MAX];
} erase_side_t;

/* Starts the erase and waits for it. Runs from RAM with interrupts off and must not call or read
 * anything in flash. */
__attribute__((section(".ramfunc"), noinline, long_call))
static void erase_run(erase_side_t *s)
{
    FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
    do {
        if (SYSTICK->CTRL & SYSTICK_COUNTFLAG)
            s->ticks++;
        if (USART1->SR & (USART_SR_RXNE | USART_SR_ORE)) {
            uint8_t b = (uint8_t)USART1->DR;
            if (s->rx_n < ERASE_RX_MAX)
                s->rx[s->rx_n++] = b;
        }
    } while (FLASH->SR & FLASH_SR_BSY);
}

#define DATA_START 0x0803B000u
#define DATA_END   0x08040000u

static int in_data_area(uint32_t addr, uint32_t len)
{
    return addr >= DATA_START && addr + len <= DATA_END && addr + len >= addr;
}

static void unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static int wait_done(void)
{
    while (FLASH->SR & FLASH_SR_BSY) {
    }
    uint32_t sr = FLASH->SR;
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
    return !(sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR));
}

int flash_erase_page(uint32_t addr)
{
    if (!in_data_area(addr, FLASH_PAGE_SIZE) || (addr & (FLASH_PAGE_SIZE - 1u)))
        return 0;
    static erase_side_t side;
    side.ticks = 0;
    side.rx_n = 0;
    uint32_t pm = irq_save();
    unlock();
    FLASH->CR = FLASH_CR_PER;
    FLASH->AR = addr;
    (void)SYSTICK->CTRL; /* clear COUNTFLAG: periods up to here belong to SysTick_Handler */
    erase_run(&side);
    int ok = wait_done();
    FLASH->CR = FLASH_CR_LOCK;
    if (side.ticks) {
        /* the periods counted in RAM also left one interrupt pending; drop it */
        SCB_ICSR = SCB_ICSR_PENDSTCLR;
        system_add_ms(side.ticks);
    }
    midi_uart_inject(side.rx, side.rx_n);
    irq_restore(pm);
    return ok;
}

int flash_program(uint32_t addr, const void *data, uint32_t len)
{
    if (!in_data_area(addr, len) || (addr & 1u))
        return 0;
    const uint8_t *src = data;
    uint32_t pm = irq_save();
    unlock();
    FLASH->CR = FLASH_CR_PG;
    int ok = 1;
    for (uint32_t i = 0; i < len && ok; i += 2) {
        uint16_t half = src[i];
        half |= (uint16_t)((i + 1 < len ? src[i + 1] : 0xFF) << 8);
        *(volatile uint16_t *)(addr + i) = half;
        ok = wait_done();
    }
    FLASH->CR = FLASH_CR_LOCK;
    irq_restore(pm);
    return ok;
}
