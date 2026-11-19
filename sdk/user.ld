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

    /* Static constructors and destructors (C++ objects, __attribute__((constructor)) and
       ((destructor))): tables of function pointers under the names newlib's
       __libc_init_array and __libc_fini_array look for. crt0.S calls the first before main
       and registers the second with atexit. The layout is ld's default one: entries with a
       priority first, in priority order, then the plain ones in link order; .ctors/.dtors
       input from an object built for the older scheme lands in the same tables. KEEP
       because nothing references the entries (--gc-sections). */
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > USER

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > USER

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
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
