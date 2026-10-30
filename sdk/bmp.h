/* bmp.h: BMP files onto the 320x240 RGB332 framebuffer (in build/libsys.a; see bmp.c).
 *
 *     struct bmp b;
 *     const char *err = bmp_open(&b, "PHOTO.BMP");           // NULL, or an error string
 *     int dw, dh; bmp_fit(b.w, b.h, 320, 240, 0, &dw, &dh);  // letterbox (1: stretch)
 *     struct bmp_target t = { page, 320, (320 - dw) / 2, (240 - dh) / 2 };
 *     err = bmp_show(&b, dw, dh, &t);                        // stream, scale, dither, write
 *     fclose(b.f);
 *
 * bmp_show reads the pixel rows from b->f (left positioned by bmp_open), scales them to
 * dw x dh with a separable box filter and Floyd-Steinberg dithers them to RGB332 into
 * t->fb at (t->ox, t->oy), t->stride bytes per line.  Rows outside the target are the
 * caller's problem: dw + ox and dh + oy must fit.  Uses malloc for its row buffers
 * (about 8 x the source row) and frees them.  bmp_show_raw copies a headerless W*H
 * RGB332 file (as written by the host build of bmpview) instead. */
#ifndef BMP_H
#define BMP_H
#include <stdio.h>
#include <stdint.h>

struct bmp {
    FILE    *f;
    int32_t  w, h;
    int      topdown;
    unsigned bpp;
    uint32_t compression;
    uint32_t data_off;
    uint32_t stride;            /* bytes per stored row (padded to 4) */
    uint32_t mask[3];           /* bit fields for 16/32 bpp */
    unsigned shift[3], bits[3];
    uint32_t pal[256];          /* 0x00RRGGBB */
    unsigned npal;
};

struct bmp_target {
    uint8_t *fb;            /* destination page (row 0 at fb) */
    int      stride;        /* bytes per line */
    int      ox, oy;        /* placement of the scaled image */
};

const char *bmp_open(struct bmp *b, const char *path);                  /* NULL or an error string */
void        bmp_decode_row(const struct bmp *b, const uint8_t *raw, uint8_t *rgb);   /* one stored row -> RGB888 */
const char *bmp_show(struct bmp *b, int dw, int dh, struct bmp_target *t);
const char *bmp_show_raw(FILE *f, long size, struct bmp_target *t, int W, int H);
void        bmp_fit(int w, int h, int W, int H, int stretch, int *dw, int *dh);   /* aspect-preserving size */

extern const uint8_t bmp_lvl3[8], bmp_lvl2[4];       /* RGB332 channel levels expanded to 8 bits */

#ifndef HOST
struct bmp_profile { uint32_t read, hbox, vbox, dither; };   /* ticks spent in each stage, cumulative */
extern struct bmp_profile bmp_prof;
#endif

#endif
