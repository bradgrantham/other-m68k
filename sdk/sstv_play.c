/* sstv_play.c: the on-target side of the SSTV encoder (see sstv.h): PCM -> PDM patterns with
 * pdm_encode, and sstv_send, which plays a framebuffer page through the BIOS audio stream.
 * The rate is the board's exact line rate, CPU_CLOCK_HZ / VIDEO_*_LINE_CLOCKS. */
#include <string.h>
#include "syscalls.h"
#include "hw.h"
#include "pdm.h"
#include "sstv.h"

int sstv_encoder_next(sstv_enc_t *e, uint8_t *out, int n)
{
    if (n > (int)sizeof e->pcm) n = (int)sizeof e->pcm;
    int got = sstv_encoder_next_pcm(e, e->pcm, n);
    if (got == 0) return 0;
    if (got < n) memset(e->pcm + got, 128, (size_t)(n - got));     /* end on the idle level */
    pdm_encode(out, e->pcm, (unsigned)n);
    return n;
}

static sstv_enc_t enc;

int sstv_send(const uint8_t *fb, uint8_t *scratch, sstv_mode_t mode, sstv_progress_fn progress, void *ctx)
{
    struct video_info vi;
    sys_video_info(&vi);
    memcpy(scratch, fb, FB_SIZE);
    uint32_t den = vi.standard == VIDEO_STD_PAL ? VIDEO_PAL_LINE_CLOCKS : VIDEO_NTSC_LINE_CLOCKS;
    if (sstv_encoder_init(&enc, scratch, (int)vi.width, (int)vi.height, (int)vi.bytes_per_line, mode, CPU_CLOCK_HZ, den) != 0)
        return -1;
    uint8_t *buf = sys_audio_start(AUDIO_BUF_MAX);
    if (!buf) return -2;
    int total = sstv_mode_lines(mode), last = -1, rc = 0;
    while (sstv_encoder_next(&enc, buf, AUDIO_BUF_MAX)) {
        buf = sys_audio_next(buf, 1);
        if (!buf) { rc = -3; break; }
        int line = sstv_encoder_line(&enc);
        if (progress && line != last && line < total) {
            last = line;
            if (progress(line, total, ctx)) { rc = 1; break; }
        }
    }
    sys_audio_last(NULL, rc == 0);                   /* abort: let the queued 66 ms play out */
    if (progress && rc == 0) progress(total, total, ctx);
    return rc;
}
