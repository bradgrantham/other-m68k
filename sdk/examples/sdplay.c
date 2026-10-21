/* sdplay: stream a sound file from the SD card through the audio hand-off API.
 *
 *   sdplay FILE.PDM          raw PDM pattern bytes, one per video line (made on a host)
 *   sdplay -pcm FILE.PCM     unsigned 8-bit PCM at the line rate (15707 Hz NTSC), encoded
 *                            on the fly with pdm_encode
 *   sdplay -half FILE.PCM    unsigned 8-bit PCM at half the line rate (7854 Hz): each
 *                            encoded pattern is played for two lines
 *
 * The BIOS buffers are below user RAM, so sys_read cannot target them directly: the file
 * is read into a program buffer and copied (512 bytes, about 1 % of the CPU).
 *
 * Reads 512-byte buffers with sys_read and hands them to sys_audio_next (blocking), so the
 * BIOS paces the program to the line rate. At the end it reports how long the stream took
 * against the ideal (bytes / line rate): any excess is time the hardware spent repeating a
 * pattern because the card or the encoder could not keep up (an underrun also switches the
 * HSYNC interrupt off until the next buffer is handed in, so the excess is a direct measure).
 * It also shows how much of the wall time was spent inside the blocking audio_next, which
 * is the CPU left over for other work. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "syscalls.h"
#include "pdm.h"

#define BUFSIZE AUDIO_BUF_MAX           /* 512 bytes = 33 ms */

static uint8_t pcm[BUFSIZE];
static uint8_t pat[BUFSIZE];

int main(int argc, char **argv)
{
    int is_pcm = 0, half = 0;
    const char *path = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-pcm")) is_pcm = 1;
        else if (!strcmp(argv[i], "-half")) is_pcm = half = 1;
        else path = argv[i];
    }
    if (!path) { printf("usage: sdplay [-pcm|-half] FILE\n"); return 1; }

    struct video_info vi;
    sys_video_info(&vi);
    uint32_t tps = vi.standard == VIDEO_STD_PAL ? 50 : 60;
    uint32_t line_hz = vi.standard == VIDEO_STD_PAL ? 15625 : 15707;

    int fd = sys_open(O_RDONLY, path);
    if (fd < 0) { printf("sdplay: cannot open %s (%d)\n", path, fd); return 1; }

    if (is_pcm) {
        printf("building PDM table... ");
        uint32_t t = sys_ticks();
        pdm_init();
        printf("%lu ms\n", (unsigned long)((sys_ticks() - t) * 1000 / tps));
    }

    uint8_t *buf = sys_audio_start(BUFSIZE);
    if (!buf) { printf("sdplay: audio_start failed\n"); return 1; }

    uint32_t bytes = 0, t0 = sys_ticks(), wait_ticks = 0;
    int n;
    for (;;) {
        if (half) {
            n = sys_read(fd, BUFSIZE / 2, pcm);
            if (n > 0) {
                if (n < BUFSIZE / 2) memset(pcm + n, 128, BUFSIZE / 2 - n);
                pdm_encode(pat, pcm, BUFSIZE / 2);
                for (int i = BUFSIZE / 2 - 1; i >= 0; i--) buf[2 * i] = buf[2 * i + 1] = pat[i];
            }
        } else if (is_pcm) {
            n = sys_read(fd, BUFSIZE, pcm);
            if (n > 0) {
                if (n < BUFSIZE) memset(pcm + n, 128, BUFSIZE - n);
                pdm_encode(buf, pcm, BUFSIZE);
            }
        } else {
            n = sys_read(fd, BUFSIZE, pat);
            if (n > 0) {
                if (n < BUFSIZE) memset(pat + n, pdm_silence(), BUFSIZE - n);
                memcpy(buf, pat, BUFSIZE);
            }
        }
        if (n <= 0) break;
        bytes += BUFSIZE;
        uint32_t tw = sys_ticks();
        buf = sys_audio_next(buf, 1);            /* blocks until a buffer is free */
        wait_ticks += sys_ticks() - tw;
        if (!buf) { printf("sdplay: audio_next failed\n"); break; }
        if (sys_pollchar() >= 0) { printf("stopped\n"); break; }
    }
    sys_audio_last(0, 1);
    uint32_t ticks = sys_ticks() - t0;
    sys_close(fd);

    uint32_t ideal_ms = (uint32_t)((uint64_t)bytes * 1000 / line_hz);
    uint32_t took_ms = ticks * 1000 / tps;
    printf("%lu bytes, ideal %lu ms, took %lu ms (%s%lu ms), %lu%% of the time waiting in audio_next\n",
           (unsigned long)bytes, (unsigned long)ideal_ms, (unsigned long)took_ms,
           took_ms >= ideal_ms ? "+" : "-",
           (unsigned long)(took_ms >= ideal_ms ? took_ms - ideal_ms : ideal_ms - took_ms),
           (unsigned long)(ticks ? wait_ticks * 100 / ticks : 0));
    if (n < 0) printf("sdplay: read error %d\n", n);
    return 0;
}
