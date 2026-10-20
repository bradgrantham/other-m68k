/* other-m68k BIOS ROM linker script. Regions come from the generated memory.ld. */
INCLUDE ../generated/memory.ld
OUTPUT_ARCH(m68k)
ENTRY(_start)

SECTIONS
{
    /* 256-entry exception vector table, first 1 KB of ROM (overlaid at 0 at reset). */
    .vectors : {
        KEEP(*(.vectors))
        . = 0x400;
    } > ROM

    .text : {
        *(.text .text.*)
        . = ALIGN(2);
    } > ROM

    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > ROM

    /* Initialised data: lives in SYSDATA, image stored in ROM, copied by crt0. */
    .data : {
        _data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        _data_end = .;
    } > SYSDATA AT> ROM
    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SYSDATA
    _end = .;

    /* Checksum word at the very end of ROM; patched by mkrom.py. */
    .romtail (ORIGIN(ROM) + LENGTH(ROM) - 2) : {
        _rom_checksum = .;
        SHORT(0xFFFF)
    } > ROM

    /DISCARD/ : { *(.comment .note* .eh_frame .debug_*) }
}
