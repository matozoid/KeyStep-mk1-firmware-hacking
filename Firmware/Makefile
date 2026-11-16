# KeyStep Mk1 open firmware
#
#   make          build build/keystep.elf / .bin (app image for 0x08004000)
#   make STOCK_USB_ID=1   use the stock USB VID/PID and product string (vendor editor support)
#   make test     build and run the host unit tests for src/core
#   make led TEMPLATE=../vendor/keystep_1_0_28.led
#                 pack build/keystep.bin into an updater .led (template stays local)

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
HOSTCC  ?= gcc

BUILD := build

CORE_SRC := $(wildcard src/core/*.c)
HW_SRC   := $(wildcard src/hw/*.c)
SRC      := $(CORE_SRC) $(HW_SRC) src/main.c
OBJ      := $(SRC:%.c=$(BUILD)/arm/%.o)

ARCH    := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(ARCH) -Os -g3 -std=c11 -Wall -Wextra -Wshadow -ffunction-sections -fdata-sections \
           -ffreestanding -fno-common -Isrc -MMD -MP
ifeq ($(STOCK_USB_ID),1)
CFLAGS  += -DSTOCK_USB_ID
endif
LDFLAGS := $(ARCH) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/keystep.map \
           -Tld/keystep_app.ld --specs=nano.specs --specs=nosys.specs

TESTS     := $(patsubst tests/%.c,%,$(wildcard tests/test_*.c))
HOSTFLAGS := -std=c11 -O1 -g -Wall -Wextra -Wshadow -fsanitize=address,undefined -Isrc

.PHONY: all test clean led

all: $(BUILD)/keystep.bin

$(BUILD)/arm/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keystep.elf: $(OBJ) ld/keystep_app.ld
	$(CC) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(BUILD)/keystep.bin: $(BUILD)/keystep.elf
	$(OBJCOPY) -O binary $< $@

src/core/hzv_table.h: tools/gen_hzv_table.py
	python3 $< $@

$(BUILD)/host/%: tests/%.c $(CORE_SRC) $(wildcard src/core/*.h) tests/test.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTFLAGS) $< $(CORE_SRC) -o $@

test: $(TESTS:%=$(BUILD)/host/%)
	@set -e; for t in $^; do $$t; done

led: $(BUILD)/keystep.bin
	@test -n "$(TEMPLATE)" || (echo "set TEMPLATE=<official .led>"; exit 1)
	python3 ../Tools/Ledpacker/make_keystep_custom_led.py $(TEMPLATE) $< $(BUILD)/keystep.led

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
