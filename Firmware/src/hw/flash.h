#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_PAGE_SIZE 0x800u

int flash_erase_page(uint32_t addr);
int flash_program(uint32_t addr, const void *data, uint32_t len);

#endif
