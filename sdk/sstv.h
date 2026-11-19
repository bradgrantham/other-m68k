/* sstv.h: slow-scan television encoder for the 320x240 RGB332 framebuffer (in build/libsys.a).
 *
 * The pure encoder (sstv_enc.c) turns an image into 8-bit unsigned PCM at any sample rate, with
 * no BIOS calls and no malloc, so it also builds on the host (tests/sstv). The on-target
 * part (sstv_play.c) encodes the PCM to PDM patterns with pdm_encode and plays a page
 * through the BIOS audio stream.
 *
 *     static uint8_t snap[FB_SIZE];
 *     pdm_init();                                                   // once, 0.4 s
 *     rc = sstv_send((const uint8_t *)vi.page_addr[0], snap, SSTV_ROBOT36, progress, ctx);
 *
 * Modes: Robot 36 (VIS 8, 240 lines, 36 s), Robot 72 (VIS 12, 240 lines, 72 s) and
 * Martin M1 (VIS 44, 256 lines of which the last 16 are black, 114 s), with a 500 ms
 * 1900 Hz lead-in before the VIS header and 500 ms of silence after the picture. Timings
 * follow JL Barber's "Proposal for SSTV Mode Specifications" (Dayton 2000). Pixel values
 * 0..255 map to 1500..2300 Hz; the Robot modes send Y, R-Y, B-Y in the BT.601 studio range,
 * Martin sends G, B, R.
 *
 * Timing is exact: segment lengths accumulate in 32.32 fixed-point samples across the whole
 * transmission, so the total length is right to within one sample however the rate divides
 * the 30 ms VIS bits or the 146.432 ms Martin scans. The rate is given as a fraction, because
 * the board's rate is CPU_CLOCK_HZ / VIDEO_NTSC_LINE_CLOCKS = 15706.806 Hz and rounding it
 * to 15707 would slant the picture by a pixel. The NCO phase (32 bits) is continuous across
 * frequency changes, so the audio has no clicks. */
#ifndef SSTV_H
#define SSTV_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SSTV_ROBOT36, SSTV_ROBOT72, SSTV_MARTIN_M1 } sstv_mode_t;

/* Progress: called once per picture line as it starts (line 0..total-1) and once more
 * with line == total when the trailer has been queued. Return nonzero to abort. */
typedef int (*sstv_progress_fn)(int line, int total, void *ctx);

#define SSTV_WIDTH       320
#define SSTV_MAX_LINES   256
#define SSTV_MAX_HDR     20                 /* header + trailer segments */

struct sstv_seg { uint8_t kind, chan; uint16_t hz; uint32_t us; };   /* a tone or a pixel scan (sstv_enc.c) */

typedef struct sstv_enc {
    const uint8_t *img;                     /* RGB332, w x h, stride bytes per line */
    int      w, h, stride;
    sstv_mode_t mode;
    uint32_t rate_num, rate_den;            /* sample rate = num / den Hz */
    uint32_t spus;                          /* samples per microsecond, 0.32 fixed */
    uint64_t step_per_hz;                   /* NCO step per Hz, 48.16 fixed */
    uint32_t lut[6][256];                   /* RGB332 byte -> NCO step, per channel */
    uint32_t line_steps[SSTV_WIDTH];        /* the current scan's steps */
    uint32_t phase;                         /* NCO phase */
    uint32_t total_samples;                 /* whole transmission */
    uint32_t emitted;
    /* sequencer */
    struct sstv_seg hdr[SSTV_MAX_HDR];      /* lead, VIS; trailer is the last entry */
    int      nhdr;
    int      part;                          /* 0 header, 1 picture lines, 2 trailer, 3 done */
    int      seg;                           /* index into the current table */
    int      line, lines;                   /* current picture line, number of lines */
    uint64_t owed;                          /* samples owed, 32.32 */
    /* current segment */
    uint32_t remaining;                     /* samples still to emit */
    int      kind;                          /* tone, silence or scan */
    uint32_t step;                          /* tone step */
    uint32_t pix, pix_step;                 /* scan position, 16.16 entries */
    uint8_t  pcm[512];                      /* sstv_encoder_next's PCM scratch */
} sstv_enc_t;

/* Pure encoder. img is w x h RGB332 with `stride` bytes per line; w must be SSTV_WIDTH and
 * h at most the mode's line count (missing lines are sent black). Returns 0, or -1 for a bad
 * argument. The total sample count is computed here (a dry run of the sequencer). */
int      sstv_encoder_init(sstv_enc_t *e, const uint8_t *img, int w, int h, int stride,
                           sstv_mode_t mode, uint32_t rate_num, uint32_t rate_den);
/* Next n samples of 8-bit unsigned PCM (128 = silence). Returns the number written: n until
 * the end, then the remainder, then 0. Any n works; segments continue across calls. */
int      sstv_encoder_next_pcm(sstv_enc_t *e, uint8_t *out, int n);
uint32_t sstv_encoder_total_samples(const sstv_enc_t *e);
int      sstv_encoder_line(const sstv_enc_t *e);       /* picture line being sent; lines when done */
int      sstv_mode_lines(sstv_mode_t mode);             /* 240 or 256 */
int      sstv_mode_vis(sstv_mode_t mode);               /* 8, 12, 44 */
uint32_t sstv_mode_line_us(sstv_mode_t mode);           /* one picture line in microseconds */
const char *sstv_mode_name(sstv_mode_t mode);           /* "Robot 36", ... */

/* Target side (sstv_play.c; needs pdm_init to have run). */
/* Next n PDM pattern bytes (n <= 512). Returns n while the picture lasts (the last buffer is
 * padded with silence), then 0. */
int      sstv_encoder_next(sstv_enc_t *e, uint8_t *out, int n);
/* Snapshot fb (FB_SIZE bytes) into scratch, then play it at the board's line rate through the
 * BIOS stream, blocking until done. progress may be NULL. Returns 0 when done, 1 when the
 * callback aborted, -1 for a bad mode, -2 if the audio stream could not be opened (a
 * callback is installed), -3 if the BIOS refused a buffer. */
int      sstv_send(const uint8_t *fb, uint8_t *scratch, sstv_mode_t mode,
                   sstv_progress_fn progress, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
