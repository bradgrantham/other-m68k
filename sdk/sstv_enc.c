/* sstv_enc.c: the pure SSTV encoder (see sstv.h). No BIOS calls, no malloc, no floating point;
 * builds for the board and for the host (tests/sstv/host_test.c includes it).
 *
 * A transmission is a sequence of segments: tones (frequency, length) and pixel scans
 * (channel, length). The header is built at init (lead-in, VIS); each picture line is one
 * of the mode's constant tables (Robot 36 alternates two); the trailer is 500 ms of silence.
 * Every segment's length in samples comes out of one 32.32 accumulator of samples owed, so
 * nothing drifts. A scan walks line_steps[] (the NCO step for each pixel of the line,
 * prepared when the scan starts) with a 16.16 pixel position; the hot loop per sample is
 * phase += steps[pix >> 16]; pix += pix_step; out = 128 + sine[phase >> 24]. */
#include <stdint.h>
#include <string.h>
#include "sstv.h"

enum { K_TONE, K_SILENCE, K_SCAN };
enum { CH_Y, CH_RY, CH_BY, CH_R, CH_G, CH_B };

#define TONE(hz, us)   { K_TONE, 0, hz, us }
#define SCAN(ch, us)   { K_SCAN, ch, 0, us }

static const struct sstv_seg robot36_even[] = {
    TONE(1200, 9000), TONE(1500, 3000), SCAN(CH_Y, 88000), TONE(1500, 4500), TONE(1900, 1500), SCAN(CH_RY, 44000) };
static const struct sstv_seg robot36_odd[] = {
    TONE(1200, 9000), TONE(1500, 3000), SCAN(CH_Y, 88000), TONE(2300, 4500), TONE(1900, 1500), SCAN(CH_BY, 44000) };
static const struct sstv_seg robot72_line[] = {
    TONE(1200, 9000), TONE(1500, 3000), SCAN(CH_Y, 138000), TONE(1500, 4500), TONE(1900, 1500), SCAN(CH_RY, 69000),
    TONE(2300, 4500), TONE(1500, 1500), SCAN(CH_BY, 69000) };
static const struct sstv_seg martin1_line[] = {
    TONE(1200, 4862), TONE(1500, 572), SCAN(CH_G, 146432), TONE(1500, 572), SCAN(CH_B, 146432), TONE(1500, 572),
    SCAN(CH_R, 146432), TONE(1500, 572) };

struct mode_desc { const char *name; uint8_t vis; uint16_t lines; const struct sstv_seg *even, *odd; uint8_t nseg; };
static const struct mode_desc modes[] = {
    { "Robot 36",  8,  240, robot36_even, robot36_odd, 6 },
    { "Robot 72",  12, 240, robot72_line, robot72_line, 9 },
    { "Martin M1", 44, 256, martin1_line, martin1_line, 8 },
};

static const int8_t sine[256] = {
       0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
      49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
      90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
     117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
     127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
     117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
      90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
      49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
       0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
     -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
     -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
     -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
     -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3,
};

static const uint8_t zero_row[SSTV_WIDTH];                /* lines past the image are black */

int sstv_mode_lines(sstv_mode_t m)      { return (unsigned)m < 3 ? modes[m].lines : 0; }
int sstv_mode_vis(sstv_mode_t m)        { return (unsigned)m < 3 ? modes[m].vis : -1; }
const char *sstv_mode_name(sstv_mode_t m) { return (unsigned)m < 3 ? modes[m].name : "?"; }
uint32_t sstv_mode_line_us(sstv_mode_t m)
{
    uint32_t us = 0;
    if ((unsigned)m >= 3) return 0;
    for (int i = 0; i < modes[m].nseg; i++) us += modes[m].even[i].us;
    return us;
}

/* NCO step for f Hz: f * 2^32 / rate, via the per-Hz step computed once at init (a 64-bit
 * division costs thousands of clocks on the 68000; the multiply a few hundred). */
static uint32_t step_for(const sstv_enc_t *e, uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz * e->step_per_hz) >> 16);
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void build_luts(sstv_enc_t *e)
{
    uint32_t s1500 = step_for(e, 1500), span = step_for(e, 2300) - s1500;
    for (int c = 0; c < 256; c++) {
        int r = ((c >> 5) * 255 + 3) / 7, g = (((c >> 2) & 7) * 255 + 3) / 7, b = (c & 3) * 85;
        /* BT.601 studio range: Y = 16 + (65.738 R + 129.057 G + 25.064 B) / 256 and the two
         * colour differences, as coefficients x1024 */
        int y  = 16  + ((263 * r + 516 * g + 100 * b + 512) >> 10);
        int ry = 128 + ((450 * r - 377 * g - 73 * b + 512) >> 10);
        int by = 128 + ((-152 * r - 298 * g + 450 * b + 512) >> 10);
        uint8_t v[6] = { clamp8(y), clamp8(ry), clamp8(by), (uint8_t)r, (uint8_t)g, (uint8_t)b };
        for (int ch = 0; ch < 6; ch++)                    /* s1500 + span * v / 255 in 32 bits */
            e->lut[ch][c] = s1500 + (span / 255) * v[ch] + ((span % 255) * v[ch] + 127) / 255;
    }
}

/* Lead-in and VIS header into e->hdr; the trailer is the final entry. */
static void build_header(sstv_enc_t *e)
{
    struct sstv_seg *s = e->hdr;
    int vis = modes[e->mode].vis, ones = 0;
    *s++ = (struct sstv_seg)TONE(1900, 500000);           /* lead-in */
    *s++ = (struct sstv_seg)TONE(1900, 300000);           /* calibration header */
    *s++ = (struct sstv_seg)TONE(1200, 10000);
    *s++ = (struct sstv_seg)TONE(1900, 300000);
    *s++ = (struct sstv_seg)TONE(1200, 30000);            /* start bit */
    for (int i = 0; i < 7; i++) {                          /* 7 data bits, LSB first */
        int bit = (vis >> i) & 1;
        ones += bit;
        *s++ = (struct sstv_seg)TONE(bit ? 1100 : 1300, 30000);
    }
    *s++ = (struct sstv_seg)TONE((ones & 1) ? 1100 : 1300, 30000);   /* even parity */
    *s++ = (struct sstv_seg)TONE(1200, 30000);            /* stop bit */
    s->kind = K_SILENCE; s->chan = 0; s->hz = 0; s->us = 500000;      /* trailer */
    s++;
    e->nhdr = (int)(s - e->hdr);
}

/* Samples for a segment of `us` microseconds, from the shared accumulator. */
static uint32_t take_samples(sstv_enc_t *e, uint32_t us)
{
    e->owed += (uint64_t)us * e->spus;
    uint32_t n = (uint32_t)(e->owed >> 32);
    e->owed &= 0xFFFFFFFFu;
    return n;
}

static const uint8_t *image_row(const sstv_enc_t *e, int y)
{
    return (y >= 0 && y < e->h) ? e->img + (size_t)y * e->stride : zero_row;
}

/* Fill line_steps for a scan of channel ch on picture line y; returns the number of entries. */
static int prepare_scan(sstv_enc_t *e, int ch, int y)
{
    const uint32_t *lut = e->lut[ch];
    uint32_t *d = e->line_steps;
    if (e->mode == SSTV_ROBOT36 && (ch == CH_RY || ch == CH_BY)) {
        /* chroma is averaged over the line pair and over horizontal pixel pairs: 160 entries */
        int y0 = y & ~1, y1 = y0 + 1;
        const uint8_t *a = image_row(e, y0), *b = image_row(e, y1);
        for (int x = 0; x < SSTV_WIDTH; x += 2)
            *d++ = (lut[a[x]] >> 2) + (lut[a[x + 1]] >> 2) + (lut[b[x]] >> 2) + (lut[b[x + 1]] >> 2);
        return SSTV_WIDTH / 2;
    }
    const uint8_t *row = image_row(e, y);
    for (int x = 0; x < SSTV_WIDTH; x++) *d++ = lut[row[x]];
    return SSTV_WIDTH;
}

/* Start the next segment. Returns 0 at the end of the transmission. dry: only count samples
 * (the init-time total), do not prepare the scan tables. */
static int advance(sstv_enc_t *e, int dry)
{
    const struct sstv_seg *s;
    for (;;) {
        if (e->part == 0) {                                /* header: all but the trailer */
            if (e->seg < e->nhdr - 1) { s = &e->hdr[e->seg++]; break; }
            e->part = 1; e->seg = 0; e->line = 0;
        }
        if (e->part == 1) {
            const struct mode_desc *m = &modes[e->mode];
            if (e->line >= e->lines) { e->part = 2; e->seg = 0; }
            else {
                const struct sstv_seg *tab = (e->line & 1) ? m->odd : m->even;
                s = &tab[e->seg++];
                if (e->seg >= m->nseg) { e->seg = 0; e->line++; }
                break;
            }
        }
        if (e->part == 2) { s = &e->hdr[e->nhdr - 1]; e->part = 3; break; }
        return 0;
    }
    uint32_t n = take_samples(e, s->us);
    e->remaining = n;
    e->kind = s->kind;
    if (s->kind == K_SCAN) {
        /* the line being prepared: advance() already moved e->line past a finished line */
        int y = (e->part == 1 && e->seg == 0) ? e->line - 1 : e->line;
        int entries = dry ? SSTV_WIDTH : prepare_scan(e, s->chan, y);
        e->pix = 0;
        e->pix_step = n ? ((uint32_t)entries << 16) / n : 0;   /* floor: pix never reaches `entries` */
    } else if (s->kind == K_TONE && !dry) {
        e->step = step_for(e, s->hz);
    }
    return 1;
}

int sstv_encoder_init(sstv_enc_t *e, const uint8_t *img, int w, int h, int stride,
                      sstv_mode_t mode, uint32_t rate_num, uint32_t rate_den)
{
    if ((unsigned)mode >= 3 || w != SSTV_WIDTH || h < 1 || !img || !rate_num || !rate_den) return -1;
    memset(e, 0, sizeof *e);
    e->img = img; e->w = w; e->h = h; e->stride = stride; e->mode = mode;
    e->rate_num = rate_num; e->rate_den = rate_den;
    e->lines = modes[mode].lines;
    if (h > e->lines) e->h = e->lines;
    /* samples per microsecond in 0.32: num * 2^32 / (den * 1e6) */
    e->spus = (uint32_t)(((uint64_t)rate_num << 32) / ((uint64_t)rate_den * 1000000u));
    /* phase step per Hz in 48.16: 2^32 * den / num, times 2^16 */
    e->step_per_hz = ((uint64_t)rate_den << 48) / rate_num;
    build_luts(e);
    build_header(e);
    /* dry run for the total: walk the sequencer without emitting */
    uint32_t total = 0;
    while (advance(e, 1)) total += e->remaining;
    e->total_samples = total;
    /* rewind */
    e->part = 0; e->seg = 0; e->line = 0; e->owed = 0; e->remaining = 0; e->emitted = 0; e->phase = 0;
    return 0;
}

uint32_t sstv_encoder_total_samples(const sstv_enc_t *e) { return e->total_samples; }

int sstv_encoder_line(const sstv_enc_t *e)
{
    if (e->part == 0) return 0;
    if (e->part >= 2) return e->lines;
    return e->seg == 0 ? e->line - 1 : e->line;        /* the line whose segments are running */
}

static void run_scan(sstv_enc_t *e, uint8_t *out, uint32_t n)
{
    uint32_t phase = e->phase, pix = e->pix, ps = e->pix_step;
    const uint32_t *steps = e->line_steps;
    for (; n; n--) {
        phase += steps[pix >> 16];
        pix += ps;
        *out++ = (uint8_t)(128 + sine[phase >> 24]);
    }
    e->phase = phase; e->pix = pix;
}

static void run_tone(sstv_enc_t *e, uint8_t *out, uint32_t n)
{
    uint32_t phase = e->phase, step = e->step;
    for (; n; n--) {
        phase += step;
        *out++ = (uint8_t)(128 + sine[phase >> 24]);
    }
    e->phase = phase;
}

int sstv_encoder_next_pcm(sstv_enc_t *e, uint8_t *out, int n)
{
    int written = 0;
    while (written < n) {
        if (e->remaining == 0 && !advance(e, 0)) break;
        uint32_t k = (uint32_t)(n - written);
        if (k > e->remaining) k = e->remaining;
        if (e->kind == K_SCAN) run_scan(e, out + written, k);
        else if (e->kind == K_TONE) run_tone(e, out + written, k);
        else { memset(out + written, 128, k); e->phase = 0; }
        e->remaining -= k;
        written += (int)k;
    }
    e->emitted += (uint32_t)written;
    return written;
}
