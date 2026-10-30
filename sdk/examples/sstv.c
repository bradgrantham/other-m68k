/* sstv: send a screen page, or a BMP file, as slow-scan television audio.
 *
 *   sstv [--page 0|1] [--mode r36|r72|m1] [--loop] [--wav PATH] [--no-ui] [FILE.BMP]
 *
 * Default: page 0 (the console) in Robot 36. The page is snapshotted first, so the progress
 * display goes on the other page: sending page 0 shows page 1 with a progress bar, sending
 * page 1 keeps the console with a status line. A BMP is fitted into 320x240 (letterboxed,
 * black borders), shown on page 1 and sent from there. --loop repeats until ESC. --wav PATH
 * writes the 8-bit mono PCM as a WAV file (D: only, the flash volume is too small) instead of
 * playing. ESC on the keyboard or on serial port A aborts. The audio rate follows the video
 * standard (NTSC 15706.8 Hz, PAL 15625 Hz), see sstv.h.
 * Exit status: 0 sent, 1 bad arguments or file, 2 aborted. Receive with the Robot36 app or
 * any SSTV decoder; a smeared picture means the CPU stalled (an interrupt storm or a flash
 * write), a slanted one means the receiver assumes the wrong sample rate. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "syscalls.h"
#include "hw.h"
#include "pdm.h"
#include "sstv.h"
#include "bmp.h"

static uint8_t snap[FB_SIZE];                 /* sstv_send's snapshot buffer */
static uint8_t pic[FB_SIZE];                  /* the decoded BMP */
static sstv_enc_t wav_enc;
static uint8_t wav_buf[16384];

struct ui {
    int      on;
    uint8_t *bar;                             /* page to draw the progress bar on, or NULL */
    int      console, row;                    /* status line on the console at this row */
    const char *name;
};

static int esc_pressed(void)
{
    int c;
    if (sys_pollchar() == 27) return 1;
    while ((c = sys_serial_getc()) >= 0) if (c == 27) return 1;
    return 0;
}

static int progress(int line, int total, void *ctx)
{
    struct ui *u = ctx;
    if (esc_pressed()) return 1;
    if (!u->on) return 0;
    if (u->bar) {
        int w = (int)((long)line * VIDEO_WIDTH / total);
        for (int y = 230; y < 234; y++) {
            memset(u->bar + y * VIDEO_BYTES_PER_LINE, 0xFF, (size_t)w);
            memset(u->bar + y * VIDEO_BYTES_PER_LINE + w, 0x49, (size_t)(VIDEO_WIDTH - w));
        }
    } else if (u->console && (line % 8 == 0 || line == total)) {
        sys_con_set_cursor(0, u->row);
        printf("sstv: %s line %3d/%d%s", u->name, line, total, line == total ? ", done   " : "");
        fflush(stdout);
    }
    return 0;
}

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 }; fwrite(b, 1, 4, f); }
static void put16(FILE *f, uint32_t v) { uint8_t b[2] = { v, v >> 8 }; fwrite(b, 1, 2, f); }

/* The PCM stream as an 8-bit mono WAV. Returns 0, 1 (refused or failed), 2 (aborted). */
static int write_wav(const char *path, const uint8_t *img, sstv_mode_t mode, uint32_t num, uint32_t den, struct ui *u)
{
    char drive = 0, cwd[128];
    if (path[0] && path[1] == ':') drive = path[0];
    else if (sys_getcwd(sizeof cwd, cwd) > 0) drive = cwd[0];
    if (drive == 'c' || drive == 'C') {
        printf("sstv: %s: the flash volume is too small for a WAV (Robot 36 is 566 KB); write to D:\n", path);
        return 1;
    }
    if (sstv_encoder_init(&wav_enc, img, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_BYTES_PER_LINE, mode, num, den) != 0) return 1;
    uint32_t total = sstv_encoder_total_samples(&wav_enc), rate = (num + den / 2) / den;
    FILE *f = fopen(path, "wb");
    if (!f) { printf("sstv: %s: %s\n", path, strerror(errno)); return 1; }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + total); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, rate); put32(f, rate); put16(f, 1); put16(f, 8);
    fwrite("data", 1, 4, f); put32(f, total);
    int lines = sstv_mode_lines(mode), last = -1, rc = 0, n;
    while ((n = sstv_encoder_next_pcm(&wav_enc, wav_buf, (int)sizeof wav_buf)) > 0) {
        if (fwrite(wav_buf, 1, (size_t)n, f) != (size_t)n) { rc = 1; break; }
        int line = sstv_encoder_line(&wav_enc);
        if (line != last && line < lines) { last = line; if (progress(line, lines, u)) { rc = 2; break; } }
    }
    int err = errno;
    if (fclose(f) != 0 && rc == 0) { rc = 1; err = errno; }
    if (rc != 0) {
        remove(path);
        if (rc == 1) printf("sstv: %s: write failed (%s)\n", path, strerror(err));
    } else if (u->on) progress(lines, lines, u);
    return rc;
}

static int usage(void)
{
    printf("usage: sstv [--page 0|1] [--mode r36|r72|m1] [--loop] [--wav PATH] [--no-ui] [FILE.BMP]\n");
    return 1;
}

int main(int argc, char **argv)
{
    int page = 0, loop = 0, noui = 0;
    sstv_mode_t mode = SSTV_ROBOT36;
    const char *wav = NULL, *file = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--page") && i + 1 < argc) { page = atoi(argv[++i]); if (page < 0 || page > 1) return usage(); }
        else if (!strcmp(a, "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (!strcmp(m, "r36") || !strcmp(m, "robot36")) mode = SSTV_ROBOT36;
            else if (!strcmp(m, "r72") || !strcmp(m, "robot72")) mode = SSTV_ROBOT72;
            else if (!strcmp(m, "m1") || !strcmp(m, "martin1")) mode = SSTV_MARTIN_M1;
            else return usage();
        }
        else if (!strcmp(a, "--loop")) loop = 1;
        else if (!strcmp(a, "--no-ui")) noui = 1;
        else if (!strcmp(a, "--wav") && i + 1 < argc) wav = argv[++i];
        else if (a[0] == '-') return usage();
        else file = a;
    }

    struct video_info vi;
    if (sys_video_info(&vi) != 0 || vi.bpp != 8) { printf("sstv: no 8 bpp framebuffer\n"); return 1; }
    uint32_t num = CPU_CLOCK_HZ, den = vi.standard == VIDEO_STD_PAL ? VIDEO_PAL_LINE_CLOCKS : VIDEO_NTSC_LINE_CLOCKS;
    uint8_t *page1 = (uint8_t *)vi.page_addr[1];
    struct ui u = { !noui, NULL, 0, 0, sstv_mode_name(mode) };
    const uint8_t *src;
    int shown = -1;                           /* display page we switched to */

    if (file) {
        struct bmp b;
        const char *err = bmp_open(&b, file);
        if (err) { if (b.f) fclose(b.f); printf("sstv: %s: %s\n", file, err); return 1; }
        int dw, dh;
        bmp_fit((int)b.w, (int)b.h, VIDEO_WIDTH, VIDEO_HEIGHT, 0, &dw, &dh);
        struct bmp_target t = { pic, VIDEO_BYTES_PER_LINE, (VIDEO_WIDTH - dw) / 2, (VIDEO_HEIGHT - dh) / 2 };
        memset(pic, 0, sizeof pic);
        err = bmp_show(&b, dw, dh, &t);
        fclose(b.f);
        if (err) { printf("sstv: %s: %s\n", file, err); return 1; }
        src = pic;
        if (!noui) { memcpy(page1, pic, FB_SIZE); sys_video_set_page(1); shown = 1; }   /* show what is sent */
    } else {
        src = (const uint8_t *)vi.page_addr[page];
        if (!noui) {
            if (page == 0) {                  /* the console is the picture: progress bar on page 1 */
                memset(page1, 0x00, FB_SIZE);
                u.bar = page1;
                sys_video_set_page(1); shown = 1;
            } else {
                struct con_info ci;
                sys_con_info(&ci);
                u.console = 1; u.row = (int)ci.cursor_y;
                if (ci.cursor_x) { printf("\n"); u.row = u.row + 1 < (int)ci.rows ? u.row + 1 : (int)ci.rows - 1; }
            }
        }
    }

    if (!wav) pdm_init();
    int rc, passes = 0;
    do {
        uint32_t t0 = sys_ticks();
        if (wav) rc = write_wav(wav, src, mode, num, den, &u);
        else {
            rc = sstv_send(src, snap, mode, progress, &u);
            if (rc < 0) { printf("sstv: audio stream unavailable (%d)\n", rc); rc = 1; break; }
            if (rc == 1) rc = 2;
        }
        uint32_t dt = sys_ticks() - t0, hz = vi.standard == VIDEO_STD_PAL ? 50 : 60;
        passes++;
        if (u.console) printf("\n");
        if (rc == 0) {
            if (wav) printf("sstv: %s, %d lines, %s in %lu.%lu s\n", sstv_mode_name(mode), sstv_mode_lines(mode), wav,
                            (unsigned long)dt / hz, (unsigned long)dt * 10 / hz % 10);
            else {
                unsigned long idle = dt ? (unsigned long)sstv_send_idle_ticks() * 100 / dt : 0;
                printf("sstv: %s, %d lines, %lu.%lu s, %lu%% CPU idle\n", sstv_mode_name(mode), sstv_mode_lines(mode),
                       (unsigned long)dt / hz, (unsigned long)dt * 10 / hz % 10, idle);
            }
        } else if (rc == 2) printf("sstv: aborted\n");
    } while (loop && rc == 0 && !esc_pressed());
    (void)passes;
    if (shown >= 0) sys_video_set_page(0);
    return rc;
}
