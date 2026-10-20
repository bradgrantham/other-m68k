/* 40x30 text console on a 320x240 RGB332 page, 8x8 font. Renders through con_fb so the
 * host tests can point it at a malloc'd buffer. Each glyph row is two aligned long writes
 * taken from a 16-entry nibble -> 4-pixel table rebuilt when the colours change. */
#include "bios.h"
#include "font8x8_basic.h"

uint8_t *con_fb;
static int cx, cy;
static uint8_t fg = 0xFF, bg = RGB332(0, 0, 96);
static uint32_t nib[16];                    /* 4 pixels per nibble, memory order */
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

static void build_tables(void)
{
    for (int n = 0; n < 16; n++)
        nib[n] = pack4((n & 1) ? fg : bg, (n & 2) ? fg : bg, (n & 4) ? fg : bg, (n & 8) ? fg : bg);
    bg4 = pack4(bg, bg, bg, bg);
}

static void draw_glyph(int x, int y, int c, int invert)
{
    const unsigned char *g = font8x8_basic[(c >= 0 && c < 128) ? c : '?'];
    uint32_t *p = (uint32_t *)(con_fb + y * 8 * VIDEO_BYTES_PER_LINE + x * 8);
    for (int r = 0; r < 8; r++) {
        unsigned row = g[r];
        if (invert) row = ~row & 0xFF;
        p[0] = nib[row & 15];
        p[1] = nib[row >> 4];
        p += VIDEO_BYTES_PER_LINE / 4;
    }
}

static void fill_rows(int row0, int nrows)
{
    uint32_t *p = (uint32_t *)(con_fb + row0 * 8 * VIDEO_BYTES_PER_LINE);
    uint32_t v = bg4;
    for (int i = nrows * 8 * VIDEO_BYTES_PER_LINE / 4; i > 0; i--)
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
    memmove(con_fb, con_fb + 8 * VIDEO_BYTES_PER_LINE, (size_t)(CON_ROWS - 1) * 8 * VIDEO_BYTES_PER_LINE);
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
const unsigned char *test_font_row(int c, int r) { return &font8x8_basic[c][r]; }
#endif
