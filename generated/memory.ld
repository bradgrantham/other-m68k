/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
MEMORY
{
    ROM (rx)      : ORIGIN = 0xE00000, LENGTH = 0x040000
    SYSDATA (rwx) : ORIGIN = 0x000400, LENGTH = 0x007C00
}
_sys_stack_top = ORIGIN(SYSDATA) + LENGTH(SYSDATA);
_rom_base = ORIGIN(ROM);
_rom_size = LENGTH(ROM);
