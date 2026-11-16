/* Internal flash page erase/program (2 KB pages on the STM32F103VC). Only the data area
 * 0x0803B000-0x0803FFFF may be written; the bootloader and the running image are refused. */
#include "flash.h"
#include "system.h"

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
    uint32_t pm = irq_save();
    unlock();
    FLASH->CR = FLASH_CR_PER;
    FLASH->AR = addr;
    FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
    int ok = wait_done();
    FLASH->CR = FLASH_CR_LOCK;
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
