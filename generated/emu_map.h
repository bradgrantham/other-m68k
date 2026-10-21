/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
#ifndef EMU_MAP_H
#define EMU_MAP_H
#include <stdint.h>
#include "hw.h"

enum emu_region_kind { EMU_RAM, EMU_ROM, EMU_IO, EMU_SWITCHES, EMU_DUART };
struct emu_region { const char *name; uint32_t base, size; enum emu_region_kind kind; };
static const struct emu_region emu_regions[] = {
    { "RAM", 0x000000, 0x040000, EMU_RAM },
    { "ROM", 0xE00000, 0x100000, EMU_ROM },
    { "IO", 0xF00000, 0x010000, EMU_IO },
    { "SWITCHES", 0xF10000, 0x010000, EMU_SWITCHES },
    { "DUART", 0xF20000, 0x010000, EMU_DUART },
};
#define EMU_REGION_COUNT 5

static const char *const emu_io_reg_names[] = {
    "INTSTAT",
    "INTEN",
    "SDIO",
    "GPIO",
    "PS2DATA",
    "?",
    "USERIO",
    "ID",
};
#define EMU_IO_REG_COUNT 8
static const char *const emu_switches_reg_names[] = {
    "PAGE0",
    "PAGE1",
    "NTSC",
    "PAL",
};
#define EMU_SW_REG_COUNT 4
static const char *const emu_duart_reg_names[] = {
    "MRA",
    "SRA/CSRA",
    "CRA",
    "RBA/TBA",
    "IPCR/ACR",
    "ISR/IMR",
    "CUR/CTUR",
    "CLR/CTLR",
    "MRB",
    "SRB/CSRB",
    "CRB",
    "RBB/TBB",
    "IVR",
    "IP/OPCR",
    "START_CTR/OPR_SET",
    "STOP_CTR/OPR_RESET",
};
#define EMU_DUART_REG_COUNT 16
static const char *const emu_syscall_names[] = {
    "exit",
    "putchar",
    "getchar",
    "pollchar",
    "puts",
    "serial_putc",
    "serial_getc",
    "ticks",
    "wait_vblank",
    "video_info",
    "video_set_page",
    "open",
    "close",
    "read",
    "write",
    "lseek",
    "opendir",
    "readdir",
    "closedir",
    "sd_read_block",
    "sd_write_block",
    "set_irq_handler",
    "mem_info",
    "con_set_cursor",
    "con_clear",
    "con_info",
    "exec",
    "key_event",
    "sleep_ms",
    "set_audio_cb",
};

#endif
