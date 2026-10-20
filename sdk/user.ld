/* other-m68k user program linker script. Region USER comes from the generated user.ld. */
INCLUDE ../generated/user.ld
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    .text : {
        KEEP(*(.text.startup))
        *(.text .text.*)
        . = ALIGN(4);
    } > USER

    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > USER

    .data : {
        *(.data .data.*)
        . = ALIGN(4);
    } > USER

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > USER
    _end = .;
    __heap_start = .;

    /DISCARD/ : { *(.comment .note* .eh_frame .debug_*) }
}
