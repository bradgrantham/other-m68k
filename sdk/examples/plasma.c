/* plasma: double-buffered RGB332 effect in 4x4 blocks. Draws into the page that is not
 * displayed, flips with sys_video_set_page, waits for VBLANK. Any key exits.
 * The sine terms are tabulated per frame (see below): 5 VBLANKs a frame at -O2 on the
 * emulator's cycle count, against 12 for the straightforward per-block version. */
#include <stdio.h>
#include <stdint.h>
#include "syscalls.h"

#define BLOCK 4

static const int8_t sintab[256] = {
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

static const uint8_t palette[256] = {   /* hue wheel */
    0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe4, 0xe4, 0xe4, 0xe4, 0xe4, 0xe4, 0xe8, 0xe8, 0xe8,
    0xe8, 0xe8, 0xe8, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf4,
    0xf4, 0xf4, 0xf4, 0xf4, 0xf4, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
    0xdc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x7c, 0x7c, 0x7c,
    0x7c, 0x7c, 0x7c, 0x7c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c,
    0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c,
    0x1c, 0x1c, 0x1c, 0x1c, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
    0x1d, 0x1d, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
    0x1f, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x13, 0x13, 0x13,
    0x13, 0x13, 0x13, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xe2, 0xe2, 0xe2, 0xe2, 0xe2, 0xe2, 0xe2, 0xe2, 0xe2, 0xe2,
    0xe2, 0xe2, 0xe2, 0xe2, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1, 0xe1,
    0xe1, 0xe1, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,
};

static inline int sn(unsigned a) { return sintab[a & 255]; }

/* Per-frame tables: each sine term depends on x, x+y or x-y alone, so it is computed once per
 * column (or diagonal) instead of once per block, and the palette is kept as the 4-byte word
 * a block row needs. The tables hold the sine values times 4 so the sum, masked, is the byte
 * offset into that word table. The inner loop is then three reads, adds, one lookup and four
 * stores; the stride is a constant so the stores are displacement-addressed. */
#define MAX_COLS 128
#define MAX_ROWS 128
#define STRIDE_L 80                      /* longwords per line: 320 bytes */
static int tx[MAX_COLS];                 /* 4 * sn(x*5 + t) */
static int txy[MAX_COLS + MAX_ROWS];     /* 4 * sn((x+y)*3 + t*3) */
static int txmy[MAX_COLS + MAX_ROWS];    /* 4 * sn((x-y)*2 + t*2), index x - y + rows */
static uint32_t pal4[256];

int main(void)
{
    struct video_info vi;
    if (sys_video_info(&vi) != 0) return 1;
    uint8_t *page[2] = { (uint8_t *)vi.page_addr[0], (uint8_t *)vi.page_addr[1] };
    unsigned cols = vi.width / BLOCK, rows = vi.height / BLOCK;
    unsigned back = 1, t = 0, frames = 0;
    uint32_t t0 = sys_ticks();

    if (cols > MAX_COLS || rows > MAX_ROWS || vi.bytes_per_line != STRIDE_L * 4) {
        printf("plasma: unexpected video mode\n");
        return 1;
    }
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = palette[i];
        pal4[i] = (c << 24) | (c << 16) | (c << 8) | c;
    }
    printf("plasma: any key exits\n");
    while (sys_pollchar() < 0) {
        uint32_t *fb = (uint32_t *)page[back];
        for (unsigned x = 0; x < cols; x++) tx[x] = 4 * sn(x * 5 + t);
        for (unsigned s = 0; s < cols + rows; s++) {
            txy[s] = 4 * sn(s * 3 + t * 3);
            txmy[s] = 4 * sn((s - rows) * 2 + t * 2);
        }
        for (unsigned y = 0; y < rows; y++) {
            uint32_t *row = fb + y * BLOCK * STRIDE_L;
            unsigned base = 4 * (512 + t + sn(y * 7 - t * 2) + sn(y * 3 + t));
            const int *px = tx, *pxy = txy + y, *pxmy = txmy + rows - y;
            for (unsigned x = cols; x--; ) {
                uint32_t q = *(const uint32_t *)((const uint8_t *)pal4 + ((base + *px++ + *pxy++ + *pxmy++) & 0x3fc));
                row[0] = q;
                row[STRIDE_L] = q;
                row[2 * STRIDE_L] = q;
                row[3 * STRIDE_L] = q;
                row++;
            }
        }
        sys_video_set_page((int)back);
        sys_wait_vblank();
        back ^= 1;
        t++;
        frames++;
    }
    uint32_t dt = sys_ticks() - t0;
    sys_video_set_page(0);
    sys_con_clear();
    printf("%u frames in %lu ticks\n", frames, (unsigned long)dt);
    return 0;
}
