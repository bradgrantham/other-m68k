/* Shared declarations for the other-m68k emulator (main.c owns the state). */
#ifndef EMU_H
#define EMU_H

#include <stdint.h>
#include <stdio.h>

struct emu_opts {
    const char *rom_path;
    const char *rom_save_path;  /* --rom-save: write the flash contents here at exit if modified */
    const char *sd_path;
    const char *serial_log_path;
    const char *duart_b_log_path;
    const char *type_text;
    const char *joy_opt;        /* --joy: switches held, e.g. "up,fire" */
    const char *paddle_opt;     /* --paddle: "A,B" (0..1023) or "pad" */
    const char *serial_type_text;
    int scale;
    int pal_jumper;
    int fast;
    int headless;
    long frames;            /* -1 = run until closed */
    int trace;
    uint32_t trace_from;
    int have_break;
    uint32_t break_addr;
    int type_delay;
    int serial_type_delay;
    int sd_trace;
    int sd_sdsc;                /* emulate a byte-addressed SDSC card */
    int io_trace;
    int exit_on_berr;
    int dump_regs;
    int no_audio;
    const char *audio_wav_path;      /* --audio-wav: rendered 48 kHz stream */
    const char *audio_edge_log_path; /* --audio-edge-log: every audio pin edge */
    int no_stdin;
    struct { uint32_t addr, len; } dump_mem[8];
    int dump_mem_count;
    const char *text_dump_path;     /* --text-dump: decode the 40x30 console text from page 0 at exit */
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
