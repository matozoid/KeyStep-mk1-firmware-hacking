// Pre-script for the KeyStep Mk1 app image (STM32F103VC, loaded at 0x08004000).
// Creates RAM / per-peripheral memory blocks so pointer literals resolve to named
// regions, then seeds functions from every vector table entry.
// @category KeyStep
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class MapMemory extends GhidraScript {
    void block(String n, long s, long len, boolean vol) throws Exception {
        try {
            MemoryBlock b = currentProgram.getMemory().createUninitializedBlock(n, toAddr(s), len, false);
            b.setRead(true); b.setWrite(true); b.setExecute(false); b.setVolatile(vol);
        } catch (Exception e) { println(n + " skipped: " + e.getMessage()); }
    }

    static final Object[][] PERIPH = {
        {"TIM2",0x40000000L},{"TIM3",0x40000400L},{"TIM4",0x40000800L},{"TIM5",0x40000C00L},
        {"TIM6",0x40001000L},{"TIM7",0x40001400L},{"RTC",0x40002800L},{"WWDG",0x40002C00L},
        {"IWDG",0x40003000L},{"SPI2",0x40003800L},{"SPI3",0x40003C00L},{"USART2",0x40004400L},
        {"USART3",0x40004800L},{"UART4",0x40004C00L},{"UART5",0x40005000L},{"I2C1",0x40005400L},
        {"I2C2",0x40005800L},{"USB",0x40005C00L},{"USB_SRAM",0x40006000L},{"CAN",0x40006400L},
        {"BKP",0x40006C00L},{"PWR",0x40007000L},{"DAC",0x40007400L},
        {"AFIO",0x40010000L},{"EXTI",0x40010400L},{"GPIOA",0x40010800L},{"GPIOB",0x40010C00L},
        {"GPIOC",0x40011000L},{"GPIOD",0x40011400L},{"GPIOE",0x40011800L},{"ADC1",0x40012400L},
        {"ADC2",0x40012800L},{"TIM1",0x40012C00L},{"SPI1",0x40013000L},{"TIM8",0x40013400L},
        {"USART1",0x40013800L},{"ADC3",0x40013C00L},{"SDIO",0x40018000L},
        {"DMA1",0x40020000L},{"DMA2",0x40020400L},{"RCC",0x40021000L},{"FLASH_IF",0x40022000L},
        {"CRC",0x40023000L},{"FSMC",0xA0000000L},
    };

    @Override public void run() throws Exception {
        block("bootloader", 0x08000000L, 0x4000, false);
        block("sram", 0x20000000L, 0xC000, false);
        for (Object[] p : PERIPH) block((String) p[0], (Long) p[1], 0x400, true);
        block("SCS", 0xE000E000L, 0x1000, true);
        block("sysmem", 0x1FFFF000L, 0x1000, false); // system memory + UID/flash size

        Address base = toAddr(0x08004000L);
        // 16 core exceptions + 60 IRQs (STM32F10x high density)
        for (int i = 1; i < 16 + 60; i++) {
            long v = currentProgram.getMemory().getInt(base.add(i * 4)) & 0xFFFFFFFFL;
            if ((v & 1) == 0 || v < 0x08004000L || v >= 0x08040000L) continue;
            Address at = toAddr(v & ~1L);
            new ArmDisassembleCommand(at, null, true).applyTo(currentProgram, monitor);
            if (getFunctionAt(at) == null) createFunction(at, null);
            addEntryPoint(at);
            if (i == 1) getFunctionAt(at).setName("Reset_Handler", SourceType.USER_DEFINED);
            else createLabel(base.add(i * 4), "vec_" + i, true);
        }
    }
}
