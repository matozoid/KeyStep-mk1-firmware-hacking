/* KeyStep Mk1 application image (STM32F103VC).
 *
 * 0x08000000-0x08003FFF  Arturia bootloader (never touched)
 * 0x08004000-0x0803AFFF  this application
 * 0x0803B000-0x0803FFFF  data pages (sequences, settings, calibration, boot flag)
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 0x37000
    DATA  (r)   : ORIGIN = 0x0803B000, LENGTH = 0x5000
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 48K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x800;

SECTIONS
{
    .isr_vector : ALIGN(4)
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : ALIGN(4)
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    _sidata = LOADADDR(.data);

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data .data.*)
        *(.ramfunc .ramfunc.*) /* code that runs while the flash is busy */
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    ASSERT(_ebss + _min_stack <= _estack, "RAM overflow")

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
