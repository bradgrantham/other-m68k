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

    /* Static constructors (C++ objects with constructors, __attribute__((constructor))):
       tables of function pointers that crt0.S walks after .data/.bss are set up and before
       bios_main, .preinit_array first. The layout is ld's default one: entries with a
       priority first, in priority order, then the plain ones in link order; .ctors input
       from an object built for the older scheme lands in the same table. KEEP because
       nothing references the entries (--gc-sections), READONLY because the compiler
       marks its entries writable, which would make the ROM segment one. The symbol names
       are the ones newlib's __libc_init_array uses, although the BIOS does not link newlib
       and crt0 does the walk itself. */
    .preinit_array (READONLY) : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > ROM

    .init_array (READONLY) : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
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

    /* Destructors are dropped: the BIOS never exits, so nothing would ever walk the table
       (and with it gone --gc-sections removes the functions it pointed at). */
    /DISCARD/ : { *(.fini_array .fini_array.* .dtors .dtors.*) }

    /DISCARD/ : { *(.comment .note* .eh_frame .debug_*) }
}
