/* Test ROM for the emulator: everything in ROM, vectors first. */
INCLUDE ../../generated/memory.ld
OUTPUT_ARCH(m68k)
ENTRY(_start)
SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > ROM
    .text    : { *(.text*) *(.rodata*) } > ROM
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
