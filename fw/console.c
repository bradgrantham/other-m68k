/* 53x24 text console on a 320x240 RGB332 page, 6x10 font (X11 misc-fixed, font6x10.h).
 * Renders through con_fb so the host tests can point it at a malloc'd buffer. A cell starts
 * at byte x*6 of its line, which is long aligned for even x and word aligned for odd x, so
 * each glyph row is one aligned long write and one aligned word write taken from a 4-pixel
 * and a 2-pixel table rebuilt when the colours change. The two pixels to the right of
 * column 52 stay background. */
#include "bios.h"
#include "font6x10.h"

_Static_assert(CON_FONT_W == FONT6X10_W && CON_FONT_H == FONT6X10_H, "console cell size");

uint8_t *con_fb;
static int cx, cy;
static uint8_t fg = 0xFF, bg = RGB332(0, 0, 96);
static uint32_t pix4[16];                   /* 4 pixels, bit 3 of the index leftmost, memory order */
static uint16_t pix2[4];                    /* 2 pixels, bit 1 leftmost */
static uint32_t bg4;
static char text[CON_ROWS][CON_COLS];       /* shadow for cursor redraw */
static int cursor_on;

static uint32_t pack4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t v;
    uint8_t t[4] = { a, b, c, d };
    __builtin_memcpy(&v, t, 4);
    return v;
}

static uint16_t pack2(uint8_t a, uint8_t b)
{
    uint16_t v;
    uint8_t t[2] = { a, b };
    __builtin_memcpy(&v, t, 2);
    return v;
}

static void build_tables(void)
{
    for (int n = 0; n < 16; n++)
        pix4[n] = pack4((n & 8) ? fg : bg, (n & 4) ? fg : bg, (n & 2) ? fg : bg, (n & 1) ? fg : bg);
    for (int n = 0; n < 4; n++)
        pix2[n] = pack2((n & 2) ? fg : bg, (n & 1) ? fg : bg);
    bg4 = pack4(bg, bg, bg, bg);
}

static void draw_glyph(int x, int y, int c, int invert)
{
    const unsigned char *g = font6x10[(c >= 0 && c < 128) ? c : '?'];
    uint8_t *p = con_fb + y * CON_FONT_H * VIDEO_BYTES_PER_LINE + x * CON_FONT_W;
    unsigned mask = invert ? 0x3F : 0;
    if (x & 1) {                            /* word at +0, long at +2 */
        for (int r = 0; r < CON_FONT_H; r++) {
            unsigned row = g[r] ^ mask;
            *(uint16_t *)p = pix2[row >> 4];
            *(uint32_t *)(p + 2) = pix4[row & 15];
            p += VIDEO_BYTES_PER_LINE;
        }
    } else {                                /* long at +0, word at +4 */
        for (int r = 0; r < CON_FONT_H; r++) {
            unsigned row = g[r] ^ mask;
            *(uint32_t *)p = pix4[row >> 2];
            *(uint16_t *)(p + 4) = pix2[row & 3];
            p += VIDEO_BYTES_PER_LINE;
        }
    }
}

static void fill_rows(int row0, int nrows)
{
    uint32_t *p = (uint32_t *)(con_fb + row0 * CON_FONT_H * VIDEO_BYTES_PER_LINE);
    uint32_t v = bg4;
    for (int i = nrows * CON_FONT_H * VIDEO_BYTES_PER_LINE / 4; i > 0; i--)
        *p++ = v;
}

void con_set_colors(uint8_t f, uint8_t b)
{
    fg = f;
    bg = b;
    build_tables();
}

void con_init(uint8_t *fb)
{
    con_fb = fb;
    build_tables();
    con_clear();
}

void con_clear(void)
{
    fill_rows(0, CON_ROWS);
    memset(text, ' ', sizeof text);
    cx = cy = 0;
    cursor_on = 0;
}

static void scroll(void)
{
    memmove(con_fb, con_fb + CON_FONT_H * VIDEO_BYTES_PER_LINE, (size_t)(CON_ROWS - 1) * CON_FONT_H * VIDEO_BYTES_PER_LINE);
    fill_rows(CON_ROWS - 1, 1);
    memmove(text[0], text[1], sizeof text - CON_COLS);
    memset(text[CON_ROWS - 1], ' ', CON_COLS);
}

static void newline(void)
{
    cx = 0;
    if (++cy >= CON_ROWS) {
        cy = CON_ROWS - 1;
        scroll();
    }
}

void con_putc(int c)
{
    int was_on = cursor_on;
    if (was_on) con_cursor_show(0);
    switch (c) {
    case '\n': newline(); break;
    case '\r': cx = 0; break;
    case '\b': if (cx > 0) cx--; else if (cx == 0 && cy > 0) { /* stay */ } break;
    case '\t': if (cx >= CON_COLS) newline(); cx = (cx + 8) & ~7; if (cx > CON_COLS) cx = CON_COLS; break;
    default:
        if (c < 0x20 || c > 0x7E) break;
        if (cx >= CON_COLS) newline();
        draw_glyph(cx, cy, c, 0);
        text[cy][cx] = (char)c;
        cx++;
        break;
    }
    if (was_on) con_cursor_show(1);
}

void con_puts(const char *s)
{
    while (*s) con_putc(*s++);
}

void con_set_cursor(int x, int y)
{
    int was_on = cursor_on;
    if (was_on) con_cursor_show(0);
    if (x < 0) x = 0;
    if (x > CON_COLS) x = CON_COLS;
    if (y < 0) y = 0;
    if (y >= CON_ROWS) y = CON_ROWS - 1;
    cx = x;
    cy = y;
    if (was_on) con_cursor_show(1);
}

void con_get_cursor(int *x, int *y)
{
    *x = cx;
    *y = cy;
}

void con_cursor_show(int on)
{
    if (on == cursor_on) return;
    cursor_on = on;
    int x = cx < CON_COLS ? cx : CON_COLS - 1;
    draw_glyph(x, cy, text[cy][x], on);
}

#ifdef HOST_TEST
const unsigned char *test_font_row(int c, int r) { return &font6x10[c][r]; }
#endif
