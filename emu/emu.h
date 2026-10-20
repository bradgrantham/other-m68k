/* Shared declarations for the other-m68k emulator (main.c owns the state). */
#ifndef EMU_H
#define EMU_H

#include <stdint.h>
#include <stdio.h>

struct emu_opts {
    const char *rom_path;
    const char *sd_path;
    const char *serial_log_path;
    const char *type_text;
    int scale;
    int pal_jumper;
    int fast;
    int headless;
    long frames;            /* -1 = run until closed */
    int trace;
    uint32_t trace_from;
    int have_break;
    uint32_t break_addr;
    int hw_serial_tx;
    int type_delay;
    int sd_trace;
    int io_trace;
    int exit_on_berr;
    int dump_regs;
    int no_audio;
    int no_stdin;
    struct { uint32_t addr, len; } dump_mem[8];
    int dump_mem_count;
};

extern struct emu_opts opts;

/* Emulated time in CPU clocks since power-up (valid inside memory handlers
 * too: it is the start time of the instruction currently executing). */
uint64_t emu_cycles(void);

/* Ask the main loop to stop after the current instruction/line; the exit
 * code is used by the process.  Safe to call from memory handlers. */
void emu_request_exit(int code);

/* Called by the bus layer when the CPU halts (double bus fault). */
void emu_cpu_halted(void);

/* Print the CPU registers (one line per group) to stderr. */
void emu_dump_regs(FILE *f);

#endif
