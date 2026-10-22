/* Host unit tests for the pure-C BIOS modules. Build/run: make -C fw test */
#include "test.h"
#include "clock.h"
#include "joy.h"

int fails, checks;

/* ---- sinks for ps2.c ---- */
static char kbd_out[256]; static int kbd_n;
static uint16_t ev_out[64]; static int ev_n;
void input_kbd_push(uint8_t c) { if (kbd_n < 255) kbd_out[kbd_n++] = (char)c; kbd_out[kbd_n] = 0; }
void input_key_event_push(uint16_t ev) { if (ev_n < 64) ev_out[ev_n++] = ev; }
static void kbd_reset(void) { kbd_n = 0; kbd_out[0] = 0; ev_n = 0; }

/* ---- stand-ins for joy.c (only joy_scale is exercised; the rest touches the DUART) ---- */
volatile uint32_t ticks;
void delay_us(uint32_t us) { (void)us; }
uint16_t irq_disable(void) { return 0; }
void irq_restore(uint16_t sr) { (void)sr; }

/* ---- DUART channel A stand-in for fs.c's /dev/ttya ---- */
char test_serial_out[256]; int test_serial_out_n;
static uint8_t serial_in[256]; static int serial_in_r, serial_in_w;
void serial_putc(int c) { if (test_serial_out_n < 255) test_serial_out[test_serial_out_n++] = (char)c; test_serial_out[test_serial_out_n] = 0; }
int serial_getc(void) { return serial_in_r < serial_in_w ? serial_in[serial_in_r++] : -1; }
void test_serial_feed(const char *s) { while (*s && serial_in_w < 256) serial_in[serial_in_w++] = (uint8_t)*s++; }
void test_serial_reset(void) { test_serial_out_n = 0; test_serial_out[0] = 0; serial_in_r = serial_in_w = 0; }
static void feed(const uint8_t *b, int n) { while (n--) ps2_feed(*b++); }

static void test_printf(void)
{
    char b[64];
    ksnprintf(b, sizeof b, "%d %u %x %X %c %s %%", -42, 3000000000u, 0xbeef, 0xBEEF, 'z', "str");
    CHECK_STR(b, "-42 3000000000 beef BEEF z str %");
    ksnprintf(b, sizeof b, "[%5d][%-5d][%05d][%08x][%3s]", 42, 42, -42, 0x1234, "abcdef");
    CHECK_STR(b, "[   42][42   ][-0042][00001234][abcdef]");
    ksnprintf(b, sizeof b, "%ld %lu %lx", -1L, 1UL, 255UL);
    CHECK_STR(b, "-1 1 ff");
    ksnprintf(b, sizeof b, "%d", -2147483647 - 1);
    CHECK_STR(b, "-2147483648");
    int n = ksnprintf(b, 4, "hello");
    CHECK(n == 5); CHECK_STR(b, "hel");
    ksnprintf(b, sizeof b, "%s", (char *)0);
    CHECK_STR(b, "(null)");
}

static void test_queue(void)
{
    uint8_t buf[8];
    struct queue q;
    queue_init(&q, buf, 8);
    CHECK(queue_empty(&q)); CHECK(queue_get(&q) == -1);
    for (int i = 0; i < 7; i++) CHECK(queue_put(&q, (uint8_t)i) == 0);
    CHECK(queue_put(&q, 99) == -1);          /* full at size-1 */
    CHECK(queue_count(&q) == 7);
    for (int i = 0; i < 7; i++) CHECK(queue_get(&q) == i);
    CHECK(queue_get(&q) == -1);
    for (int r = 0; r < 20; r++) { queue_put(&q, (uint8_t)r); CHECK(queue_get(&q) == r); }  /* wrap */
}

/* ---- srec ---- */
static uint8_t mem[0x100];
static void store(void *ctx, uint32_t addr, const uint8_t *d, int n)
{
    (void)ctx;
    if (addr + (uint32_t)n <= sizeof mem) memcpy(mem + addr, d, (size_t)n);
}

static void test_srec(void)
{
    struct srec_state s;
    srec_init(&s, store, 0);
    CHECK(srec_line(&s, "S00F000068656C6C6F202020202000003C") == 0);          /* header */
    CHECK(srec_line(&s, "S1130010285F245F2212226A000424290008237C1A") == 0);  /* 16 bytes @ 0x10 */
    CHECK(s.records == 1 && s.bytes == 16 && s.min_addr == 0x10 && s.max_addr == 0x1F);
    CHECK(mem[0x10] == 0x28 && mem[0x11] == 0x5F && mem[0x1F] == 0x7C);
    CHECK(srec_line(&s, "S1130010285F245F2212226A000424290008237C1B") == -1); /* bad checksum */
    CHECK(s.errors == 1);
    CHECK(srec_line(&s, "S20800400001020304AD\r\n") == 0);                     /* S2, 4 bytes @ 0x4000 (CRLF ok) */
    CHECK(s.max_addr == 0x4003 && s.bytes == 20);
    CHECK(srec_line(&s, "S30900000020AABBCCDDC8") == 0);                       /* S3 @ 0x20 */
    CHECK(mem[0x20] == 0xAA && mem[0x23] == 0xDD);
    CHECK(srec_line(&s, "S5030003F9") == 0);                                   /* count */
    CHECK(srec_line(&s, "garbage") == -1);
    CHECK(srec_line(&s, "S1130010285F") == -1);                                /* short */
    CHECK(!s.done);
    CHECK(srec_line(&s, "S9030020DC") == 1);                                   /* entry 0x20 */
    CHECK(s.done && s.have_entry && s.entry == 0x20);
    srec_init(&s, store, 0);
    CHECK(srec_line(&s, "S804004000BB") == 1); CHECK(s.entry == 0x4000);
    srec_init(&s, store, 0);
    CHECK(srec_line(&s, "S70500004000BA") == 1); CHECK(s.entry == 0x4000);
    CHECK(srec_line(&s, "S1130010285F245F2212226A000424290008237C1Ax") == -1); /* trailing junk */
}

static void test_ps2(void)
{
    ps2_init(); kbd_reset();
    static const uint8_t seq1[] = { 0x1C, 0xF0, 0x1C };                /* a */
    feed(seq1, sizeof seq1);
    CHECK_STR(kbd_out, "a");
    CHECK(ev_n == 2 && ev_out[0] == 0x1C && ev_out[1] == (0x1C | KEY_RELEASE));
    kbd_reset();
    static const uint8_t seq2[] = { 0x12, 0x1C, 0xF0, 0x1C, 0xF0, 0x12, 0x16, 0x12, 0x16, 0xF0, 0x16, 0xF0, 0x12 };
    feed(seq2, sizeof seq2);                                             /* Shift+a, 1, Shift+1 */
    CHECK_STR(kbd_out, "A1!");
    kbd_reset();
    static const uint8_t seq3[] = { 0x58, 0xF0, 0x58, 0x1C, 0xF0, 0x1C, 0x12, 0x1C, 0xF0, 0x1C, 0xF0, 0x12, 0x58, 0xF0, 0x58 };
    feed(seq3, sizeof seq3);                                             /* caps on: a -> A, shift+a -> a, caps off */
    CHECK_STR(kbd_out, "Aa");
    kbd_reset();
    static const uint8_t seq4[] = { 0x14, 0x21, 0xF0, 0x21, 0xF0, 0x14 }; /* ctrl+c */
    feed(seq4, sizeof seq4);
    CHECK(kbd_n == 1 && kbd_out[0] == 3);
    kbd_reset();
    static const uint8_t seq5[] = { 0xE0, 0x75, 0xE0, 0xF0, 0x75, 0xE0, 0x6B, 0xE0, 0xF0, 0x6B };  /* up, left */
    feed(seq5, sizeof seq5);
    CHECK_STR(kbd_out, "\033[A\033[D");
    CHECK(ev_n == 4 && ev_out[0] == (0x75 | KEY_EXTENDED) && ev_out[1] == (0x75 | KEY_EXTENDED | KEY_RELEASE));
    kbd_reset();
    static const uint8_t seq6[] = { 0x05, 0xF0, 0x05, 0x03, 0xF0, 0x03, 0x07, 0xF0, 0x07, 0x5A, 0xF0, 0x5A, 0x66, 0x76 };
    feed(seq6, sizeof seq6);                                             /* F1 F5 F12 Enter BS Esc */
    CHECK_STR(kbd_out, "\033OP\033[15~\033[24~\r\b\033");
    kbd_reset();
    static const uint8_t seq7[] = { 0xE0, 0x71, 0xE0, 0xF0, 0x71, 0xE0, 0x7D, 0xE0, 0x6C, 0xE0, 0x69 };
    feed(seq7, sizeof seq7);                                             /* Del PgUp Home End */
    CHECK_STR(kbd_out, "\033[3~\033[5~\033[H\033[F");
    kbd_reset();
    static const uint8_t seq8[] = { 0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77, 0x29 }; /* Pause then space */
    feed(seq8, sizeof seq8);
    CHECK_STR(kbd_out, " ");
    kbd_reset();
    static const uint8_t seq9[] = { 0xFA, 0xAA, 0x12, 0x4E, 0xF0, 0x4E, 0xF0, 0x12, 0x55 };  /* ack, bat, Shift+- , = */
    feed(seq9, sizeof seq9);
    CHECK_STR(kbd_out, "_=");
}

/* ---- console ---- */
static uint8_t *fb;
static int glyph_matches(int x, int y, int c, uint8_t fgc, uint8_t bgc)
{
    extern const unsigned char *test_font_row(int c, int r);
    for (int r = 0; r < 8; r++) {
        unsigned row = *test_font_row(c, r);
        for (int px = 0; px < 8; px++) {
            uint8_t want = (row >> px) & 1 ? fgc : bgc;
            if (fb[(y * 8 + r) * VIDEO_BYTES_PER_LINE + x * 8 + px] != want) return 0;
        }
    }
    return 1;
}

static void test_console(void)
{
    int x, y;
    fb = malloc(FB_SIZE);
    memset(fb, 0x5A, FB_SIZE);
    con_set_colors(0xFF, 0x01);
    con_init(fb);
    for (size_t i = 0; i < FB_SIZE; i++) if (fb[i] != 0x01) { CHECK(!"clear"); break; }
    con_putc('A');
    CHECK(glyph_matches(0, 0, 'A', 0xFF, 0x01));
    con_get_cursor(&x, &y); CHECK(x == 1 && y == 0);
    con_puts("\tB");
    con_get_cursor(&x, &y); CHECK(x == 9 && y == 0);
    CHECK(glyph_matches(8, 0, 'B', 0xFF, 0x01));
    con_puts("\b\bC");                      /* non-destructive backspace, then overwrite */
    CHECK(glyph_matches(7, 0, 'C', 0xFF, 0x01));
    CHECK(glyph_matches(8, 0, 'B', 0xFF, 0x01));
    con_puts("\r\n");
    con_get_cursor(&x, &y); CHECK(x == 0 && y == 1);
    for (int i = 0; i < 40; i++) con_putc('x');   /* fill line 1 exactly: cursor parks at col 40 */
    con_get_cursor(&x, &y); CHECK(x == 40 && y == 1);
    con_putc('\n');                                /* no double newline */
    con_get_cursor(&x, &y); CHECK(x == 0 && y == 2);
    for (int i = 0; i < 41; i++) con_putc('y');   /* wraps on the 41st */
    con_get_cursor(&x, &y); CHECK(x == 1 && y == 3);
    con_set_cursor(0, CON_ROWS - 1);
    con_puts("Z\n");                               /* scroll: 'A' moves off, 'Z' row 28 */
    con_get_cursor(&x, &y); CHECK(x == 0 && y == CON_ROWS - 1);
    CHECK(glyph_matches(0, CON_ROWS - 2, 'Z', 0xFF, 0x01));
    CHECK(glyph_matches(7, 0, 'x', 0xFF, 0x01));   /* old line 1 now at line 0 */
    for (int px = 0; px < VIDEO_BYTES_PER_LINE * 8; px++)
        if (fb[(CON_ROWS - 1) * 8 * VIDEO_BYTES_PER_LINE + px] != 0x01) { CHECK(!"last row cleared"); break; }
    con_set_cursor(5, 5); con_putc('Q');
    con_set_cursor(5, 5); con_cursor_show(1);
    CHECK(glyph_matches(5, 5, 'Q', 0x01, 0xFF));  /* inverted while the cursor is shown */
    con_cursor_show(0);
    CHECK(glyph_matches(5, 5, 'Q', 0xFF, 0x01));
    con_set_colors(0xE0, 0x00);
    con_set_cursor(0, 0); con_putc('R');
    CHECK(glyph_matches(0, 0, 'R', 0xE0, 0x00));
    free(fb);
}

static void test_hexdump(void)
{
    char line[64];
    uint8_t d[8] = { 0x00, 0x41, 0x7E, 0x7F, 0x20, 0xFF, 0x1F, 0x61 };
    hexdump_line(line, 0x4000, d, 8);
    CHECK_STR(line, "004000 00 41 7e 7f 20 ff 1f 61 .A~. ..a");
    CHECK(strlen(line) < 40);
    hexdump_line(line, 0xE1FFFE, d, 2);
    CHECK_STR(line, "e1fffe 00 41                   .A");
}

/* ---- clock.c: calendar arithmetic and the frame-driven second ---- */
void test_clock(void)
{
    struct clock_tm t;
    uint32_t s;
    char b[20];
    static const struct { uint32_t secs; int y, mo, d, h, mi, se; } v[] = {
        { 0, 1970, 1, 1, 0, 0, 0 }, { 315532800u, 1980, 1, 1, 0, 0, 0 }, { 951868800u, 2000, 3, 1, 0, 0, 0 },
        { 1709251198u, 2024, 2, 29, 23, 59, 58 }, { 1792618200u, 2026, 10, 21, 21, 30, 0 },
        { 2147483648u, 2038, 1, 19, 3, 14, 8 }, { 4107542400u, 2100, 3, 1, 0, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        clock_to_tm(v[i].secs, &t);
        CHECK(t.year == v[i].y && t.mon == v[i].mo && t.day == v[i].d && t.hour == v[i].h && t.min == v[i].mi && t.sec == v[i].se);
        CHECK_EQ(clock_from_tm(&t), v[i].secs);
        clock_format(v[i].secs, b);
        CHECK(clock_parse(b, &s) && s == v[i].secs);
    }
    clock_format(1792618200u, b); CHECK_STR(b, "2026-10-21 21:30:00");
    CHECK(clock_parse("2026-10-21 21:30", &s) && s == 1792618200u);
    CHECK(clock_parse("2026-10-21", &s) && s == 1792618200u - 21 * 3600 - 30 * 60);
    CHECK(clock_parse("2026-10-21  21:30:07 ", &s) && s == 1792618207u);
    CHECK(!clock_parse("2026-13-01", &s)); CHECK(!clock_parse("2026-02-29", &s)); CHECK(clock_parse("2024-02-29", &s));
    CHECK(!clock_parse("2026-10-21 24:00", &s)); CHECK(!clock_parse("26-10-21", &s)); CHECK(!clock_parse("2026-10-21x", &s));
    CHECK(!clock_parse("1969-12-31", &s));
    /* FAT stamps: 2-second resolution, 1980 base */
    uint32_t ft = clock_to_fat(1792618207u);
    CHECK_EQ(ft >> 16, ((2026 - 1980) << 9) | (10 << 5) | 21); CHECK_EQ(ft & 0xFFFF, (21 << 11) | (30 << 5) | 3);
    CHECK_EQ(clock_from_fat(ft >> 16, ft & 0xFFFF), 1792618206u);
    CHECK_EQ(clock_from_fat(0, 0), 0);
    CHECK_EQ(clock_to_fat(0) >> 16, (0 << 9) | (1 << 5) | 1);              /* clamps to 1980-01-01 */
    /* the second advances from frames: 60 NTSC frames (200168 clocks each) pass 12 MHz once */
    clock_set(0); clock_tick(); CHECK_EQ(clock_get(), 0);                  /* unset: stays 0 */
    clock_set(100);
    for (int i = 0; i < 59; i++) clock_tick();
    CHECK_EQ(clock_get(), 100);
    clock_tick(); CHECK_EQ(clock_get(), 101);
    clock_add_frames(600); CHECK_EQ(clock_get(), 111);
    clock_set(0);
}

/* ---- joy.c: the paddle scale (the rest talks to the DUART) ---- */
static void test_joy(void)
{
    CHECK_EQ(joy_scale(0), 0);
    CHECK_EQ(joy_scale(2764), 1023);                   /* 12 ms at 230.4 kHz */
    CHECK_EQ(joy_scale(1382), 511);
    CHECK_EQ(joy_scale(5000), 1023);                   /* past full scale: clamped */
    CHECK_EQ(joy_scale(65535), 1023);
}

int main(int argc, char **argv)
{
    test_printf();
    test_queue();
    test_srec();
    test_ps2();
    test_console();
    test_hexdump();
    test_sd();
    test_audio();
    test_elf(argc > 2 ? argv[2] : 0);
    test_clock();
    test_joy();
    test_fs(argc > 1 ? argv[1] : 0, argc > 3 ? argv[3] : 0);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
