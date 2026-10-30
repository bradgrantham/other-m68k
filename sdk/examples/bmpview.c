/* bmpview: show a BMP file on the 320x240 RGB332 framebuffer.
 *
 *   run bmpview.elf PHOTO.BMP          fit inside the screen, aspect preserved, letterboxed
 *   run bmpview.elf PHOTO.BMP -s       stretch to the full screen
 *
 * The reader, the streaming box scaler and the Floyd-Steinberg dither live in ../bmp.c
 * (libsys), shared with sstv; this file is the command line and the page handling.
 *
 * Build for the host with -DHOST (sdk Makefile target `host-bmpview`): then it is
 *   bmpview in.bmp out.ppm|out.332 [width height] [-s]
 * and writes the dithered RGB332 image expanded to 24-bit PPM (or the raw screen), for
 * checking the algorithm.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bmp.h"

#ifndef HOST
#include "syscalls.h"
#endif

#ifdef HOST
/* ------------------------------------------------------------------ host harness */
int main(int argc, char **argv)
{
    int W = 320, H = 240, stretch = 0;
    const char *in = NULL, *outp = NULL;
    int dims[2], nd = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s")) stretch = 1;
        else if (!in) in = argv[i];
        else if (!outp) outp = argv[i];
        else if (nd < 2) dims[nd++] = atoi(argv[i]);
    }
    if (!in || !outp) { fprintf(stderr, "usage: bmpview in.bmp out.ppm|out.332 [width height] [-s]\n"); return 2; }
    if (nd == 2) { W = dims[0]; H = dims[1]; }
    struct bmp b;
    const char *err = bmp_open(&b, in);
    if (err) { fprintf(stderr, "%s: %s\n", in, err); return 1; }
    int dw, dh; bmp_fit(b.w, b.h, W, H, stretch, &dw, &dh);
    uint8_t *fb = calloc((size_t)W * H, 1);
    struct bmp_target t = { fb, W, (W - dw) / 2, (H - dh) / 2 };
    fprintf(stderr, "%s: %ldx%ld %u bpp%s -> %dx%d at %d,%d\n", in, (long)b.w, (long)b.h, b.bpp, b.topdown ? " top-down" : "", dw, dh, t.ox, t.oy);
    err = bmp_show(&b, dw, dh, &t);
    fclose(b.f);
    if (err) { fprintf(stderr, "%s: %s\n", in, err); return 1; }
    FILE *o = fopen(outp, "wb");
    if (!o) { perror(outp); return 1; }
    size_t ol = strlen(outp);
    if (ol > 4 && !strcmp(outp + ol - 4, ".332")) {
        fwrite(fb, 1, (size_t)W * H, o);            /* raw screen for the board to blit */
    } else {
        fprintf(o, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; i++) {
            uint8_t c = fb[i], px[3] = { bmp_lvl3[c >> 5], bmp_lvl3[(c >> 2) & 7], bmp_lvl2[c & 3] };
            fwrite(px, 1, 3, o);
        }
    }
    fclose(o);
    return 0;
}
#else
/* ------------------------------------------------------------------ board */
int main(int argc, char **argv)
{
    const char *path = NULL;
    int stretch = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s")) stretch = 1;
        else path = argv[i];
    }
    if (!path) { printf("usage: run bmpview.elf FILE.BMP [-s]\n"); return 2; }

    struct video_info vi;
    if (sys_video_info(&vi) != 0 || vi.bpp != 8) { printf("bmpview: no 8 bpp framebuffer\n"); return 1; }
    struct bmp b;
    const char *err = bmp_open(&b, path);
    int show = vi.current_page == 0 ? 1 : 0;            /* draw on the page the console is not using */
    struct bmp_target t = { (uint8_t *)vi.page_addr[show], (int)vi.bytes_per_line, 0, 0 };
    if (err && b.f) {                                   /* not a BMP: maybe a raw screen */
        fseek(b.f, 0, SEEK_END);
        long size = ftell(b.f);
        printf("%s: raw RGB332 screen, any key returns\n", path);
        sys_video_set_page(show);
        err = bmp_show_raw(b.f, size, &t, (int)vi.width, (int)vi.height);
    } else if (!err) {
        int dw, dh;
        bmp_fit(b.w, b.h, (int)vi.width, (int)vi.height, stretch, &dw, &dh);
        t.ox = ((int)vi.width - dw) / 2; t.oy = ((int)vi.height - dh) / 2;
        printf("%s: %ldx%ld %u bpp -> %dx%d, any key returns\n", path, (long)b.w, (long)b.h, b.bpp, dw, dh);
        memset(t.fb, 0, (size_t)vi.bytes_per_line * vi.height);
        sys_video_set_page(show);
        err = bmp_show(&b, dw, dh, &t);
    }
    if (b.f) fclose(b.f);
    if (err) { sys_video_set_page((int)vi.current_page); printf("bmpview: %s: %s\n", path, err); return 1; }
    sys_getchar();
    sys_video_set_page((int)vi.current_page);
    return 0;
}
#endif
