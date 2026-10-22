/* other-m68k emulator: Musashi 68000 + the two CPLDs + peripherals.
 *
 * Timing model: the CPU runs one scanline at a time (line_clocks cycles,
 * from the NTSC/PAL table latched at the start of each frame).  Each slice
 * begins at the HSYNC leading edge: the HSYNC flag (and VBLANK on the first
 * vsync line) is raised, then m68k_execute() runs until the line's cycle
 * budget is spent; the overshoot of the last instruction is carried into the
 * next line so long-term timing is exact.  Memory handlers that change the
 * interrupt level end the Musashi timeslice so the CPU samples IPL after the
 * current instruction (Musashi only checks interrupts at m68k_execute()
 * entry and on SR writes).  Peripherals that need the clock (DUART, console
 * RX, PS/2 delivery) are polled at the end of each line; the
 * framebuffer is converted at the end of the last active line and shown,
 * paced to real time, at the end of the frame.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL.h>

#include "hw.h"
#include "emu_map.h"
#include "m68k.h"
#include "emu.h"
#include "bus.h"
#include "io.h"
#include "video.h"
#include "serial.h"
#include "duart.h"
#include "ps2.h"
#include "sdcard.h"
#include "joy.h"
#include "audio.h"

struct emu_opts opts = {
    .scale = 3,
    .frames = -1,
    .type_delay = 120,
    .serial_type_delay = 120,
};

static uint64_t cycle_base;         /* cycles completed before the current m68k_execute() */
static int in_cpu;
static int exit_requested;
static int exit_code;
static int cpu_halted;
static int trace_active;
static long frames_done;
static int sdl_inited;
static volatile sig_atomic_t got_signal;

/* ---- services for the other modules ---------------------------------------- */

uint64_t emu_cycles(void)
{
    return cycle_base + (in_cpu ? (uint64_t)m68k_cycles_run() : 0);
}

void emu_request_exit(int code)
{
    if (!exit_requested) {
        exit_requested = 1;
        exit_code = code;
    }
    if (in_cpu)
        m68k_end_timeslice();
}

void emu_cpu_halted(void)
{
    if (!cpu_halted) {
        cpu_halted = 1;
        fprintf(stderr, "m68k-emu: CPU halted (double bus fault) at PC=%06X; %s\n",
                m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF,
                opts.headless ? "exiting" : "press F11 to reset");
        if (opts.headless)
            emu_request_exit(3);
    }
}

void emu_dump_regs(FILE *f)
{
    static const m68k_register_t dregs[8] = {
        M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3,
        M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    };
    static const m68k_register_t aregs[8] = {
        M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3,
        M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
    };
    for (int i = 0; i < 8; i++)
        fprintf(f, "D%d=%08X%s", i, m68k_get_reg(NULL, dregs[i]), i == 7 ? "\n" : " ");
    for (int i = 0; i < 8; i++)
        fprintf(f, "A%d=%08X%s", i, m68k_get_reg(NULL, aregs[i]), i == 7 ? "\n" : " ");
    fprintf(f, "PC=%08X SR=%04X USP=%08X SSP=%08X cycles=%llu\n",
            m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SR) & 0xFFFF,
            m68k_get_reg(NULL, M68K_REG_USP), m68k_get_reg(NULL, M68K_REG_ISP),
            (unsigned long long)emu_cycles());
}

/* ---- Musashi callbacks --------------------------------------------------------- */

static int int_ack(int level)
{
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;     /* CPLD1 asserts VPA on every IACK */
}

static void reset_instr(void)
{
    /* The RESET instruction does not reach the CPLDs (architecture.md 6). */
}

static void instr_hook(unsigned int pc)
{
    pc &= 0xFFFFFF;
    if (opts.have_break && pc == opts.break_addr && !exit_requested) {
        fprintf(stderr, "m68k-emu: breakpoint at %06X (cycle %llu)\n", pc,
                (unsigned long long)emu_cycles());
        emu_dump_regs(stderr);
        emu_request_exit(0);
    }
    if (opts.trace) {
        if (!trace_active && pc == opts.trace_from)
            trace_active = 1;
        if (trace_active) {
            char dis[128];
            m68k_disassemble(dis, pc, M68K_CPU_TYPE_68000);
            fprintf(stderr, "%06X: %-40s SR=%04X D0=%08X D1=%08X D2=%08X D3=%08X A0=%08X A1=%08X A7=%08X\n",
                    pc, dis, m68k_get_reg(NULL, M68K_REG_SR) & 0xFFFF,
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                    m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
                    m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
                    m68k_get_reg(NULL, M68K_REG_A7));
        }
    }
}

/* ---- reset --------------------------------------------------------------------- */

static void system_reset(void)
{
    bus_reset();
    io_reset();
    video_reset();
    duart_reset();
    sd_reset();
    ps2_reset();
    audio_reset();
    cpu_halted = 0;
    m68k_pulse_reset();                 /* reads SSP/PC through the overlay */
    io_update_ipl();
}

/* ---- host events --------------------------------------------------------------- */

static void handle_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            emu_request_exit(0);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int down = (ev.type == SDL_KEYDOWN);
            SDL_Scancode sc = ev.key.keysym.scancode;
            if (sc == SDL_SCANCODE_F11) {
                if (down && !ev.key.repeat) {
                    fprintf(stderr, "m68k-emu: reset button\n");
                    system_reset();
                }
            } else if (sc == SDL_SCANCODE_F12 || sc == SDL_SCANCODE_PAUSE) {
                if (!ev.key.repeat)
                    io_set_nmi(down);
            } else {
                ps2_key(sc, down);   /* repeats become typematic make codes */
            }
            break;
        }
        default:
            joy_event(&ev);
            break;
        }
    }
}

/* ---- pacing -------------------------------------------------------------------- */

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void pace(double frame_seconds)
{
    static double deadline;
    double now = now_seconds();
    if (deadline == 0 || now - deadline > 0.25)
        deadline = now;             /* (re)synchronise after a stall */
    deadline += frame_seconds;
    double wait = deadline - now;
    if (wait > 0) {
        struct timespec ts = { (time_t)wait, (long)((wait - (double)(time_t)wait) * 1e9) };
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
            ;
    }
}

/* ---- command line ----------------------------------------------------------------- */

static void usage(FILE *f)
{
    fprintf(f,
        "usage: m68k-emu --rom FILE [options]\n"
        "  --rom FILE          ROM image (<= 1 MB, padded with 0xFF)\n"
        "  --rom-save FILE     write the flash contents to FILE at exit if they were reprogrammed\n"
        "  --sd FILE           SD card image (multiple of 512 bytes, read/write)\n"
        "  --scale N           window scale factor (default 3)\n"
        "  --pal               fit the PAL jumper (INTSTAT.PALJMP = 1)\n"
        "  --fast              do not pace to real time (also disables audio)\n"
        "  --headless          no window, no audio\n"
        "  --frames N          exit after N frames\n"
        "  --trace             disassemble every instruction to stderr\n"
        "  --trace-from ADDR   start tracing when PC first reaches ADDR\n"
        "  --break ADDR        dump registers and exit when PC reaches ADDR\n"
        "  --serial-log FILE   also write serial (DUART channel A) output to FILE\n"
        "  --duart-b-log FILE  write DUART channel B output to FILE (dropped otherwise)\n"
        "  --type STR          type STR on the PS/2 keyboard (\\n = Enter)\n"
        "  --joy LIST          hold joystick switches: up,down,left,right,fire (a game controller also works)\n"
        "  --paddle A,B|pad    paddles on the port at A,B (0..1023), or from a controller's triggers\n"
        "  --type-delay N      ... after N frames (default 120)\n"
        "  --serial-type STR   feed STR into the serial port (DUART channel A, \\n = CR)\n"
        "  --serial-type-delay N  ... after N frames (default 120)\n"
        "  --sd-trace          log SD card commands to stderr\n"
        "  --sd-sdsc           emulate a byte-addressed SDSC card (CSD v1) instead of SDHC\n"
        "  --io-trace          log CPLD2 register accesses and soft switches to stderr\n"
        "  --exit-on-berr      exit with status 2 on the first bus error\n"
        "  --dump-regs-on-exit print the CPU registers at exit\n"
        "  --dump-mem ADDR LEN hex dump memory at exit (repeatable)\n"
        "  --text-dump FILE    at exit, write the 40x30 console text decoded from page 0 to FILE\n"
        "                      (- for stdout); the console is not on serial after the POST\n"
        "  --no-audio          disable audio output\n"
        "  --audio-wav FILE    write the rendered audio (48 kHz, 16-bit mono WAV) to FILE;\n"
        "                      works with --headless and --fast too\n"
        "  --audio-edge-log FILE  log every audio pin edge as 'cycle level' (debugging)\n"
        "  --no-stdin          do not read serial RX bytes from stdin\n"
        "Keys: F11 reset button, F12 or Pause NMI button, others -> PS/2 keyboard.\n"
        "Serial (DUART channel A) output goes to stdout; diagnostics go to stderr.\n");
}

static uint32_t parse_num(const char *s, const char *what)
{
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0);
    if (errno || *end || end == s) {
        fprintf(stderr, "m68k-emu: bad %s '%s'\n", what, s);
        exit(1);
    }
    return (uint32_t)v;
}

static const char *need_arg(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "m68k-emu: %s needs an argument\n", argv[*i]);
        exit(1);
    }
    return argv[++*i];
}

static void parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--rom")) opts.rom_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--rom-save")) opts.rom_save_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--sd")) opts.sd_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--scale")) opts.scale = (int)parse_num(need_arg(argc, argv, &i), "scale");
        else if (!strcmp(a, "--pal")) opts.pal_jumper = 1;
        else if (!strcmp(a, "--fast")) opts.fast = 1;
        else if (!strcmp(a, "--headless")) opts.headless = 1;
        else if (!strcmp(a, "--frames")) opts.frames = (long)parse_num(need_arg(argc, argv, &i), "frame count");
        else if (!strcmp(a, "--trace")) opts.trace = 1;
        else if (!strcmp(a, "--trace-from")) {
            opts.trace = 1;
            opts.trace_from = parse_num(need_arg(argc, argv, &i), "address") & 0xFFFFFF;
        }
        else if (!strcmp(a, "--break")) {
            opts.have_break = 1;
            opts.break_addr = parse_num(need_arg(argc, argv, &i), "address") & 0xFFFFFF;
        }
        else if (!strcmp(a, "--serial-log")) opts.serial_log_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--duart-b-log")) opts.duart_b_log_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--type")) opts.type_text = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--joy")) opts.joy_opt = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--paddle")) opts.paddle_opt = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--type-delay")) opts.type_delay = (int)parse_num(need_arg(argc, argv, &i), "frame count");
        else if (!strcmp(a, "--serial-type")) opts.serial_type_text = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--serial-type-delay")) opts.serial_type_delay = (int)parse_num(need_arg(argc, argv, &i), "frame count");
        else if (!strcmp(a, "--sd-trace")) opts.sd_trace = 1;
        else if (!strcmp(a, "--sd-sdsc")) opts.sd_sdsc = 1;
        else if (!strcmp(a, "--io-trace")) opts.io_trace = 1;
        else if (!strcmp(a, "--exit-on-berr")) opts.exit_on_berr = 1;
        else if (!strcmp(a, "--dump-regs-on-exit")) opts.dump_regs = 1;
        else if (!strcmp(a, "--text-dump")) opts.text_dump_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--dump-mem")) {
            if (opts.dump_mem_count >= 8) {
                fprintf(stderr, "m68k-emu: too many --dump-mem\n");
                exit(1);
            }
            opts.dump_mem[opts.dump_mem_count].addr = parse_num(need_arg(argc, argv, &i), "address");
            opts.dump_mem[opts.dump_mem_count].len = parse_num(need_arg(argc, argv, &i), "length");
            opts.dump_mem_count++;
        }
        else if (!strcmp(a, "--no-audio")) opts.no_audio = 1;
        else if (!strcmp(a, "--audio-wav")) opts.audio_wav_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--audio-edge-log")) opts.audio_edge_log_path = need_arg(argc, argv, &i);
        else if (!strcmp(a, "--no-stdin")) opts.no_stdin = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            exit(0);
        }
        else {
            fprintf(stderr, "m68k-emu: unknown option '%s'\n", a);
            usage(stderr);
            exit(1);
        }
    }
    if (!opts.rom_path) {
        fprintf(stderr, "m68k-emu: --rom is required\n");
        usage(stderr);
        exit(1);
    }
    if (opts.scale < 1) opts.scale = 1;
    if (opts.scale > 16) opts.scale = 16;
}

/* ---- main loop --------------------------------------------------------------------- */

static void on_signal(int sig)
{
    (void)sig;
    got_signal = 1;
}

static void cleanup(void)
{
    serial_shutdown();              /* flushes the logs, restores the tty */
    sd_shutdown();
    audio_shutdown();
    video_shutdown();
    if (sdl_inited)
        SDL_Quit();
}

static void run_line(int line_clocks)
{
    static int carry;
    int target = line_clocks - carry;
    int run = 0;
    while (run < target && !exit_requested) {
        in_cpu = 1;
        int r = m68k_execute(target - run);
        in_cpu = 0;
        cycle_base += (uint64_t)r;
        run += r;
    }
    carry = run - target;
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);

    if (bus_load_rom(opts.rom_path) != 0)
        return 1;
    if (serial_init(opts.serial_log_path, opts.duart_b_log_path, !opts.no_stdin) != 0)
        return 1;
    atexit(cleanup);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    if (joy_init(opts.joy_opt, opts.paddle_opt) != 0)
        return 1;
    if (sd_init(opts.sd_path, opts.sd_trace, opts.sd_sdsc) != 0)
        return 1;

    if (!opts.headless) {
        if (SDL_Init(0) != 0) {
            fprintf(stderr, "m68k-emu: SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        sdl_inited = 1;
        joy_open_controller();
    }
    if (video_init(opts.scale, opts.headless) != 0)
        return 1;
    if (audio_init(!opts.headless && !opts.fast && !opts.no_audio,
                   opts.audio_wav_path, opts.audio_edge_log_path) != 0)
        return 1;

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(int_ack);
    m68k_set_reset_instr_callback(reset_instr);
    if (opts.trace || opts.have_break)
        m68k_set_instr_hook_callback(instr_hook);
    if (opts.trace && opts.trace_from == 0)
        trace_active = 1;

    system_reset();

    int typed = (opts.type_text == NULL);
    int serial_typed = (opts.serial_type_text == NULL);
    while (!exit_requested) {
        int v = 0;
        for (;;) {
            video_line_start(v, cycle_base);
            const struct video_timing *t = video_timing();
            run_line(t->line_clocks);
            duart_tick(cycle_base);
            ps2_tick(cycle_base);
            if (v == t->v_active - 1)
                video_render();
            if (got_signal)
                emu_request_exit(0);
            if (exit_requested)
                break;
            if (++v >= t->lines)
                break;
        }
        if (exit_requested)
            break;
        frames_done++;

        io_pdm_advance(cycle_base);     /* rotations of the last line before rendering */
        audio_frame(cycle_base);
        if (!opts.headless) {
            handle_events();
            video_present();
        }
        if (!typed && frames_done >= opts.type_delay) {
            ps2_type_text(opts.type_text);
            typed = 1;
        }
        if (!serial_typed && frames_done >= opts.serial_type_delay) {
            serial_inject_text(opts.serial_type_text);
            serial_typed = 1;
        }
        if (opts.frames >= 0 && frames_done >= opts.frames)
            break;
        if (!opts.fast && !opts.headless)
            pace(1.0 / video_timing()->frame_hz);
    }

    serial_flush();
    fprintf(stderr, "m68k-emu: %ld frames, %llu cycles, %lu serial bytes out, %lu in (%lu overruns, %lu lost), "
            "%lu bus errors%s\n",
            frames_done, (unsigned long long)cycle_base, duart_tx_bytes[0], duart_rx_bytes, duart_rx_overruns,
            duart_rx_dropped, bus_berr_count, cpu_halted ? ", CPU halted" : "");
    if (opts.dump_regs)
        emu_dump_regs(stderr);
    for (int i = 0; i < opts.dump_mem_count; i++)
        bus_dump(opts.dump_mem[i].addr, opts.dump_mem[i].len);
    if (opts.text_dump_path) {
        FILE *f = !strcmp(opts.text_dump_path, "-") ? stdout : fopen(opts.text_dump_path, "w");
        if (!f) perror(opts.text_dump_path);
        else { video_text_dump(f); if (f != stdout) fclose(f); }
    }
    if (opts.rom_save_path) {
        if (bus_rom_modified)
            bus_save_rom(opts.rom_save_path);
        else
            fprintf(stderr, "m68k-emu: ROM not modified, %s not written\n", opts.rom_save_path);
    }
    return exit_code;
}
