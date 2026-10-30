/* bmp.c: BMP reader, streaming separable box scaler and Floyd-Steinberg dither to RGB332.
 * See bmp.h for the API and examples/bmpview.c for the viewer built on it.
 *
 * The file is streamed one row at a time (a 1920-wide 24-bit row is 5.8 KB, the whole
 * image would not fit in user RAM), so the scaler is separable and streaming:
 *
 *   1. each source row is decoded to RGB888 and box-filtered horizontally: source pixel x
 *      covers [x*dw, (x+1)*dw) and destination pixel d covers [d*sw, (d+1)*sw) on a common
 *      grid; the overlap lengths, scaled to 8.8 fixed point so they sum to exactly 256 per
 *      destination pixel, are precomputed once per image (struct axis), so a pixel costs
 *      three 16x16 multiplies and the normalisation is a shift;
 *   2. the resulting dw-pixel row is box-filtered vertically with a second plan into one
 *      16-bit accumulator row; whenever a destination row is complete it is normalised,
 *      Floyd-Steinberg dithered to RGB332 (serpentine scan, 7/16 3/16 5/16 1/16) and
 *      written to the target.
 *
 * Works for both down- and up-scaling.  Supports uncompressed BMP with 1, 4, 8 (palette),
 * 16 (555 or bit fields), 24 and 32 (xRGB or bit fields) bits per pixel, bottom-up or
 * top-down.  RLE is not supported.  Builds for the host with -DHOST. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bmp.h"

#ifndef HOST
#include "syscalls.h"
#endif

/* ------------------------------------------------------------------ BMP reading */

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void mask_setup(struct bmp *b, int c, uint32_t m)
{
    unsigned s = 0, n = 0;
    b->mask[c] = m;
    if (m) { while (!((m >> s) & 1)) s++; while (((m >> (s + n)) & 1) && s + n < 32) n++; }
    b->shift[c] = s; b->bits[c] = n;
}

const char *bmp_open(struct bmp *b, const char *path)
{
    uint8_t hdr[14 + 124];
    memset(b, 0, sizeof *b);
    b->f = fopen(path, "rb");
    if (!b->f) return "cannot open file";
    if (fread(hdr, 1, 14 + 4, b->f) != 18 || hdr[0] != 'B' || hdr[1] != 'M') return "not a BMP file";
    uint32_t ihsize = rd32(hdr + 14);
    if (ihsize < 40 || ihsize > 124) return "unsupported BMP header";
    if (fread(hdr + 18, 1, ihsize - 4, b->f) != ihsize - 4) return "short header";
    b->data_off = rd32(hdr + 10);
    b->w = (int32_t)rd32(hdr + 18);
    b->h = (int32_t)rd32(hdr + 22);
    b->bpp = rd16(hdr + 28);
    b->compression = rd32(hdr + 30);
    uint32_t colors_used = rd32(hdr + 46);
    if (b->h < 0) { b->topdown = 1; b->h = -b->h; }
    if (b->w <= 0 || b->h <= 0 || b->w > 4096 || b->h > 4096) return "bad image size (max 4096x4096)";
    if (b->compression != 0 && b->compression != 3) return "compressed BMP not supported";
    switch (b->bpp) {
    case 1: case 4: case 8:
        b->npal = colors_used ? colors_used : (1u << b->bpp);
        if (b->npal > 256) return "bad palette size";
        {
            uint8_t pe[4];
            if (fseek(b->f, 14 + (long)ihsize, SEEK_SET) != 0) return "seek failed";
            for (unsigned i = 0; i < b->npal; i++) {
                if (fread(pe, 1, 4, b->f) != 4) return "short palette";
                b->pal[i] = ((uint32_t)pe[2] << 16) | (pe[1] << 8) | pe[0];
            }
        }
        break;
    case 16:
        if (b->compression == 3) { mask_setup(b, 0, rd32(hdr + 54)); mask_setup(b, 1, rd32(hdr + 58)); mask_setup(b, 2, rd32(hdr + 62)); }
        else { mask_setup(b, 0, 0x7C00); mask_setup(b, 1, 0x03E0); mask_setup(b, 2, 0x001F); }
        break;
    case 24:
        break;
    case 32:
        if (b->compression == 3) { mask_setup(b, 0, rd32(hdr + 54)); mask_setup(b, 1, rd32(hdr + 58)); mask_setup(b, 2, rd32(hdr + 62)); }
        else { mask_setup(b, 0, 0x00FF0000); mask_setup(b, 1, 0x0000FF00); mask_setup(b, 2, 0x000000FF); }
        break;
    default:
        return "unsupported bits per pixel";
    }
    b->stride = (((uint32_t)b->w * b->bpp + 31) / 32) * 4;
    if (fseek(b->f, (long)b->data_off, SEEK_SET) != 0) return "seek failed";
    return NULL;
}

static uint8_t expand(uint32_t v, unsigned bits)      /* n-bit channel -> 8 bits */
{
    if (bits == 0) return 0;
    if (bits >= 8) return (uint8_t)(v >> (bits - 8));
    uint32_t r = v << (8 - bits);
    r |= r >> bits;                                     /* replicate */
    if (bits < 4) r |= r >> (2 * bits);
    return (uint8_t)r;
}

/* Decode one stored row (raw) into RGB888 triples (rgb), w pixels. */
void bmp_decode_row(const struct bmp *b, const uint8_t *raw, uint8_t *rgb)
{
    int w = b->w;
    switch (b->bpp) {
    case 24:
        for (int x = 0; x < w; x++, raw += 3, rgb += 3) { rgb[0] = raw[2]; rgb[1] = raw[1]; rgb[2] = raw[0]; }
        break;
    case 32:
        for (int x = 0; x < w; x++, raw += 4, rgb += 3) {
            uint32_t v = rd32(raw);
            for (int c = 0; c < 3; c++) rgb[c] = expand((v & b->mask[c]) >> b->shift[c], b->bits[c]);
        }
        break;
    case 16:
        for (int x = 0; x < w; x++, raw += 2, rgb += 3) {
            uint32_t v = rd16(raw);
            for (int c = 0; c < 3; c++) rgb[c] = expand((v & b->mask[c]) >> b->shift[c], b->bits[c]);
        }
        break;
    case 8:
        for (int x = 0; x < w; x++, rgb += 3) {
            uint32_t p = b->pal[raw[x] < b->npal ? raw[x] : 0];
            rgb[0] = p >> 16; rgb[1] = p >> 8; rgb[2] = p;
        }
        break;
    case 4:
        for (int x = 0; x < w; x++, rgb += 3) {
            unsigned i = (x & 1) ? (raw[x >> 1] & 15) : (raw[x >> 1] >> 4);
            uint32_t p = b->pal[i < b->npal ? i : 0];
            rgb[0] = p >> 16; rgb[1] = p >> 8; rgb[2] = p;
        }
        break;
    case 1:
        for (int x = 0; x < w; x++, rgb += 3) {
            unsigned i = (raw[x >> 3] >> (7 - (x & 7))) & 1;
            uint32_t p = b->pal[i < b->npal ? i : 0];
            rgb[0] = p >> 16; rgb[1] = p >> 8; rgb[2] = p;
        }
        break;
    }
}

/* ------------------------------------------------------------------ box scaling */
/* One axis of the separable box filter as a precomputed plan: destination element d is
 * the weighted sum of source elements s0[d] .. s0[d]+cnt[d]-1 with 8.8 fixed-point weights
 * w[start[d] ..] that sum to exactly 256 (largest-remainder rounding of the overlap
 * lengths), so normalising is a shift and a 16-bit accumulator never overflows
 * (256 * 255 = 65280).  The 68000 has only a 16x16->32 multiply; all operands fit. */
struct axis {
    int       sn, dn;
    uint16_t *s0, *cnt, *start;     /* [dn] */
    uint16_t *w;                    /* [sn + dn] is enough: each boundary adds one pair */
};

static inline uint16_t mul16(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a * b); }

static int axis_build(struct axis *ax, int sn, int dn)
{
    ax->sn = sn; ax->dn = dn;
    ax->s0 = malloc((size_t)dn * sizeof *ax->s0);
    ax->cnt = malloc((size_t)dn * sizeof *ax->cnt);
    ax->start = malloc((size_t)dn * sizeof *ax->start);
    ax->w = malloc((size_t)(sn + dn) * sizeof *ax->w);
    if (!ax->s0 || !ax->cnt || !ax->start || !ax->w) return -1;
    int np = 0;
    for (int d = 0; d < dn; d++) {
        /* common grid: source s spans [s*dn, (s+1)*dn), destination d spans [d*sn, (d+1)*sn) */
        uint32_t lo = (uint32_t)d * sn, hi = lo + sn;
        int sa = (int)(lo / dn), sb = (int)((hi - 1) / dn);     /* first and last source element */
        ax->s0[d] = (uint16_t)sa; ax->cnt[d] = (uint16_t)(sb - sa + 1); ax->start[d] = (uint16_t)np;
        uint32_t sum = 0, best_rem = 0; int best = np;
        for (int si = sa; si <= sb; si++) {
            uint32_t a = (uint32_t)si * dn, b = a + dn;
            uint32_t seg = (b < hi ? b : hi) - (a > lo ? a : lo);
            uint32_t wq = seg * 256 / sn, rem = seg * 256 % sn;
            ax->w[np] = (uint16_t)wq; sum += wq;
            if (rem > best_rem) { best_rem = rem; best = np; }
            np++;
        }
        ax->w[best] += (uint16_t)(256 - sum);      /* make the weights sum to exactly 256 */
    }
    return 0;
}

static void axis_free(struct axis *ax) { free(ax->s0); free(ax->cnt); free(ax->start); free(ax->w); }

/* Horizontal pass: one decoded RGB888 row -> dw RGB888 pixels. */
__attribute__((noinline))
static void box_row(const struct axis *ax, const uint8_t *src, uint8_t *dst)
{
    int dn = ax->dn;
    const uint16_t *w = ax->w;
    for (int d = 0; d < dn; d++) {
        const uint8_t *p = src + ax->s0[d] * 3;
        int n = ax->cnt[d];
        uint16_t a0 = 128, a1 = 128, a2 = 128;            /* +0.5 for rounding */
        do {
            uint16_t wt = *w++;
            a0 += mul16(wt, p[0]); a1 += mul16(wt, p[1]); a2 += mul16(wt, p[2]);
            p += 3;
        } while (--n);
        dst[0] = (uint8_t)(a0 >> 8); dst[1] = (uint8_t)(a1 >> 8); dst[2] = (uint8_t)(a2 >> 8);
        dst += 3;
    }
}

/* Vertical pass, streaming: add row (dw RGB888 pixels) with weight wt to the 16-bit
 * accumulators; emit normalises them into out. */
__attribute__((noinline))
static void vacc_add(uint16_t *acc, const uint8_t *row, int n3, uint16_t wt)
{
    if (wt == 256) { for (int i = 0; i < n3; i++) acc[i] += (uint16_t)(row[i] << 8); }
    else           { for (int i = 0; i < n3; i++) acc[i] += mul16(wt, row[i]); }
}

/* ------------------------------------------------------------------ dithering */
/* Floyd-Steinberg to RGB332 with serpentine scanning.  err_cur/err_next: (dw + 2) * 3
 * int16 each, index pixel+1 so the edge spills are harmless.  Row direction alternates. */
const uint8_t bmp_lvl3[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
const uint8_t bmp_lvl2[4] = { 0, 85, 170, 255 };
static uint8_t q3tab[256], q2tab[256];     /* 8-bit value -> nearest 3-bit / 2-bit level */

static void dither_init(void)
{
    for (int v = 0; v < 256; v++) { q3tab[v] = (uint8_t)((v * 7 + 127) / 255); q2tab[v] = (uint8_t)((v * 3 + 127) / 255); }
}

__attribute__((noinline))
static void dither_row(const uint8_t *rgb, int dw, uint8_t *out, int16_t *err_cur, int16_t *err_next, int reverse)
{
    memset(err_next, 0, (size_t)(dw + 2) * 3 * sizeof *err_next);
    int step = reverse ? -1 : 1;
    int x = reverse ? dw - 1 : 0;
    const uint8_t *p = rgb + x * 3;
    int16_t *e  = err_cur  + (x + 1) * 3;       /* this pixel's accumulated error */
    int16_t *en = err_next + (x + 1) * 3;       /* same column on the next row */
    int pstep = 3 * step;
    /* error carried to the next pixel in scan direction, per channel (the 7/16 share) */
    int16_t carry0 = 0, carry1 = 0, carry2 = 0;
    for (int n = 0; n < dw; n++) {
        int16_t v, er, e7, e3, e5;
        unsigned q0, q1, q2;
        /* red: 3 bits */
        v = (int16_t)(p[0] + e[0] + carry0); if (v < 0) v = 0; else if (v > 255) v = 255;
        q0 = q3tab[v]; er = (int16_t)(v - bmp_lvl3[q0]);
        e7 = (int16_t)((er * 7) >> 4); e3 = (int16_t)((er * 3) >> 4); e5 = (int16_t)((er * 5) >> 4);
        carry0 = e7; en[-pstep + 0] += e3; en[0] += e5; en[pstep + 0] += (int16_t)(er - e7 - e3 - e5);
        /* green: 3 bits */
        v = (int16_t)(p[1] + e[1] + carry1); if (v < 0) v = 0; else if (v > 255) v = 255;
        q1 = q3tab[v]; er = (int16_t)(v - bmp_lvl3[q1]);
        e7 = (int16_t)((er * 7) >> 4); e3 = (int16_t)((er * 3) >> 4); e5 = (int16_t)((er * 5) >> 4);
        carry1 = e7; en[-pstep + 1] += e3; en[1] += e5; en[pstep + 1] += (int16_t)(er - e7 - e3 - e5);
        /* blue: 2 bits */
        v = (int16_t)(p[2] + e[2] + carry2); if (v < 0) v = 0; else if (v > 255) v = 255;
        q2 = q2tab[v]; er = (int16_t)(v - bmp_lvl2[q2]);
        e7 = (int16_t)((er * 7) >> 4); e3 = (int16_t)((er * 3) >> 4); e5 = (int16_t)((er * 5) >> 4);
        carry2 = e7; en[-pstep + 2] += e3; en[2] += e5; en[pstep + 2] += (int16_t)(er - e7 - e3 - e5);

        out[x] = (uint8_t)((q0 << 5) | (q1 << 2) | q2);
        x += step; p += pstep; e += pstep; en += pstep;
    }
}

/* ------------------------------------------------------------------ the pipeline */

/* ------------------------------------------------------------------ raw RGB332 blob */
/* A file of exactly width*height bytes (no header) is a pre-dithered RGB332 screen as
 * produced by the host build (`bmpview in.bmp out.332 320 240`): just copy it in. */
const char *bmp_show_raw(FILE *f, long size, struct bmp_target *t, int W, int H)
{
    if (size != (long)W * H) return "not a BMP and not a raw RGB332 screen";
    if (fseek(f, 0, SEEK_SET) != 0) return "seek failed";
    for (int y = 0; y < H; y++)
        if (fread(t->fb + (size_t)y * t->stride, 1, (size_t)W, f) != (size_t)W) return "short raw data";
    return NULL;
}


const char *bmp_show(struct bmp *b, int dw, int dh, struct bmp_target *t)
{
    int sw = b->w, sh = b->h;
    struct axis hx = {0}, vx = {0};
    uint8_t  *raw  = malloc(b->stride);
    uint8_t  *rgb  = malloc((size_t)sw * 3);
    uint8_t  *hrow = malloc((size_t)dw * 3);
    uint16_t *vacc = malloc((size_t)dw * 3 * sizeof *vacc);
    uint8_t  *drow = malloc((size_t)dw * 3);
    uint8_t  *out  = malloc((size_t)dw);
    int16_t  *e0   = malloc((size_t)(dw + 2) * 3 * sizeof *e0);
    int16_t  *e1   = malloc((size_t)(dw + 2) * 3 * sizeof *e1);
    const char *err = NULL;
    if (!raw || !rgb || !hrow || !vacc || !drow || !out || !e0 || !e1 ||
        axis_build(&hx, sw, dw) != 0 || axis_build(&vx, sh, dh) != 0) { err = "out of memory"; goto done; }
    memset(e0, 0, (size_t)(dw + 2) * 3 * sizeof *e0);
    dither_init();

    int n3 = dw * 3;
    int pending = -1;                   /* destination row being accumulated (file order) */
    int reverse = 0;
    for (int y = 0; y < sh; y++) {
        if (fread(raw, 1, b->stride, b->f) != b->stride) { err = "short pixel data"; goto done; }
        bmp_decode_row(b, raw, rgb);
        box_row(&hx, rgb, hrow);
        /* destination rows touched by source row y: those whose span intersects [y*dh, (y+1)*dh) */
        int r_lo = (int)(((uint32_t)y * dh) / sh), r_hi = (int)((((uint32_t)y + 1) * dh - 1) / sh);
        for (int r = r_lo; r <= r_hi; r++) {
            if (r != pending) { memset(vacc, 0, (size_t)n3 * sizeof *vacc); pending = r; }
            vacc_add(vacc, hrow, n3, vx.w[vx.start[r] + (y - vx.s0[r])]);
            if (y == vx.s0[r] + vx.cnt[r] - 1) {                        /* last source row of r */
                for (int i = 0; i < n3; i++) drow[i] = (uint8_t)((vacc[i] + 128) >> 8);
                pending = -1;
                dither_row(drow, dw, out, e0, e1, reverse);
                int16_t *tmp = e0; e0 = e1; e1 = tmp;
                reverse ^= 1;
                int row = b->topdown ? r : dh - 1 - r;                  /* file order -> screen row */
                memcpy(t->fb + (size_t)(t->oy + row) * t->stride + t->ox, out, (size_t)dw);
            }
        }
    }
done:
    axis_free(&hx); axis_free(&vx);
    free(raw); free(rgb); free(hrow); free(vacc); free(drow); free(out); free(e0); free(e1);
    return err;
}

/* Fit w x h into W x H keeping the aspect ratio (or stretch). */
void bmp_fit(int w, int h, int W, int H, int stretch, int *dw, int *dh)
{
    if (stretch) { *dw = W; *dh = H; return; }
    /* compare w/h with W/H without floating point */
    if ((long)w * H >= (long)h * W) { *dw = W; *dh = (int)(((long)h * W + w / 2) / w); }
    else                            { *dh = H; *dw = (int)(((long)w * H + h / 2) / h); }
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
}
