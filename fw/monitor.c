/* Machine monitor: line editor, S-record upload (explicit `srec` or auto-detected from a
 * line starting with "S<digit>"), memory commands and user program launch. */
#include "bios.h"

#define LINE_MAX 96
#define SREC_LINE_MAX 520

uint32_t last_entry = USER_BASE;
static uint32_t md_next = USER_BASE;
static uint32_t srec_rejected;

/* ---- S-record receive ------------------------------------------------------------ */

static void srec_store(void *ctx, uint32_t addr, const uint8_t *data, int n)
{
    (void)ctx;
    /* Only RAM above the BIOS area may be written: the stack and queues live in SYSDATA. */
    if (addr < USER_BASE || addr + (uint32_t)n > RAM_END) { srec_rejected++; return; }
    memcpy((void *)addr, data, (size_t)n);
}

/* Line terminators: CR, LF or CRLF. A LF directly after a CR is part of the same end of
 * line (S-record files and most terminals send CRLF). */
static int prev_cr;
static int getchar_eol(void)
{
    for (;;) {
        int c = input_getchar();
        int skip = (c == '\n' && prev_cr);
        prev_cr = (c == '\r');
        if (!skip) return c;
    }
}

/* Read one raw line without echo. Returns length, or -1 on ESC. */
static int read_raw_line(char *buf, int max)
{
    int n = 0;
    for (;;) {
        int c = getchar_eol();
        if (c == 0x1B) return -1;
        if (c == '\r' || c == '\n') { buf[n] = 0; return n; }
        if (n < max - 1) buf[n++] = (char)c;
    }
}

/* `first` is the already-read start of the first line ("S" + digit) or NULL. Nothing is
 * echoed while records arrive: at 38400 baud a record line lands every ~11 ms, and
 * echoing it (console glyphs, a 35 ms scroll, the serial copy) would take longer than
 * that, so the 512-byte RX ring would overflow within a few lines. The DUART's 3-byte
 * FIFO is drained by the level-3 ISR, which keeps running during the parse. */
static void srec_receive(const char *first)
{
    static char line[SREC_LINE_MAX];
    struct srec_state s;
    srec_init(&s, srec_store, 0);
    srec_rejected = 0;
    int n;
    if (first) {
        size_t l = strlen(first);
        memcpy(line, first, l);
        n = read_raw_line(line + l, (int)(SREC_LINE_MAX - l));
        if (n < 0) { kprintf("aborted\n"); return; }
        n += (int)l;
    } else {
        kprintf("send S-records (ESC aborts)\n");
        n = read_raw_line(line, SREC_LINE_MAX);
        if (n < 0) { kprintf("aborted\n"); return; }
    }
    for (;;) {
        if (n > 0) {
            if (line[0] != 'S' && line[0] != 's') break;   /* a non-record line ends the upload */
            if (srec_line(&s, line) == 1) break;
        }
        n = read_raw_line(line, SREC_LINE_MAX);
        if (n < 0) break;
    }
    kprintf("srec: %u records, %u bytes", s.records, s.bytes);
    if (s.have_data) kprintf(", %06x-%06x", s.min_addr, s.max_addr);
    if (s.have_entry) { kprintf(", entry %06x", s.entry); last_entry = s.entry; }
    else if (s.have_data) last_entry = s.min_addr;
    if (s.errors) kprintf(", %u bad lines", s.errors);
    if (srec_rejected) kprintf(", %u records outside %06x-%06x", srec_rejected,
                               (unsigned)USER_BASE, (unsigned)(RAM_END - 1));
    kprintf("\n");
}

/* ---- line editor ------------------------------------------------------------------ */

/* Returns the line length, or -1 if an S-record upload was handled instead. */
static int read_line(char *buf, int max)
{
    int n = 0, pending_s = 0;
    con_cursor_show(1);
    for (;;) {
        int c = getchar_eol();
        if (pending_s) {
            pending_s = 0;
            if (c >= '0' && c <= '9') {
                char first[3] = { 'S', (char)c, 0 };
                con_cursor_show(0);
                srec_receive(first);
                return -1;
            }
            putchar_both('S');
            buf[n++] = 'S';
        }
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            con_cursor_show(0);
            putchar_both('\n');
            return n;
        }
        if (c == '\b' || c == 0x7F) {
            if (n > 0) { n--; putchar_both('\b'); putchar_both(' '); putchar_both('\b'); }
            continue;
        }
        if (c == 0x15 || c == 0x03) {           /* ^U / ^C: kill line */
            while (n > 0) { n--; putchar_both('\b'); putchar_both(' '); putchar_both('\b'); }
            continue;
        }
        if (c == 0x1B) {                        /* swallow escape sequences from arrow keys */
            int c2 = input_getchar();
            if (c2 == '[' || c2 == 'O') {
                int c3;
                do { c3 = input_getchar(); } while (c3 >= '0' && c3 <= '9');
            }
            continue;
        }
        if (c < 0x20 || c > 0x7E) continue;
        if (n == 0 && c == 'S') { pending_s = 1; continue; }   /* hold the echo */
        if (n < max - 1) { buf[n++] = (char)c; putchar_both(c); }
    }
}

/* ---- argument parsing --------------------------------------------------------------- */

static int parse_hex(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int digits = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else return 0;
        v = (v << 4) | (uint32_t)d;
        digits++;
    }
    if (!digits) return 0;
    *out = v;
    return 1;
}

static int split(char *line, char **argv, int max)
{
    int argc = 0;
    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) *line++ = 0;
    }
    return argc;
}

static int need_hex(int argc, char **argv, int i, uint32_t *v)
{
    if (i >= argc || !parse_hex(argv[i], v)) { kprintf("bad or missing hex argument\n"); return 0; }
    return 1;
}

/* ---- commands ----------------------------------------------------------------------- */

static void cmd_help(void)
{
    kprintf("help           this list (hex args)\n"
            "md [a] [len]   dump memory\n"
            "mb/mw/ml a v.. write bytes/words/longs\n"
            "fill a len v   fill bytes\n"
            "go [a]         run in user mode\n"
            "regs           last exception\n"
            "srec           receive S-records\n"
            "reset led n  page n  ticks  cls\n"
            "ramtest  beep  info\n"
            "dir run cat sd (stage 2)\n");
}

static void cmd_md(int argc, char **argv)
{
    uint32_t addr = md_next, len = 64;
    if (argc > 1 && !need_hex(argc, argv, 1, &addr)) return;
    if (argc > 2 && !need_hex(argc, argv, 2, &len)) return;
    if (len > 0x10000) len = 0x10000;
    int abortable = len > 256;      /* long dumps stop on any key; short ones must not eat input */
    char line[48];
    uint8_t tmp[HEXDUMP_BYTES_PER_LINE];
    while (len) {
        int n = len < HEXDUMP_BYTES_PER_LINE ? (int)len : HEXDUMP_BYTES_PER_LINE;
        for (int i = 0; i < n; i++) tmp[i] = *(volatile uint8_t *)(addr + (uint32_t)i);
        hexdump_line(line, addr, tmp, n);
        kprintf("%s\n", line);
        addr += (uint32_t)n;
        len -= (uint32_t)n;
        if (abortable && input_pollchar() >= 0) break;
    }
    md_next = addr;
}

static void cmd_mem_write(int argc, char **argv, int width)
{
    uint32_t addr, v;
    if (!need_hex(argc, argv, 1, &addr)) return;
    if (argc < 3) { kprintf("value(s) missing\n"); return; }
    if (width > 1 && (addr & 1)) { kprintf("odd address\n"); return; }
    for (int i = 2; i < argc; i++) {
        if (!need_hex(argc, argv, i, &v)) return;
        switch (width) {
        case 1: *(volatile uint8_t *)addr = (uint8_t)v; break;
        case 2: *(volatile uint16_t *)addr = (uint16_t)v; break;
        default: *(volatile uint32_t *)addr = v; break;
        }
        addr += (uint32_t)width;
    }
}

static void cmd_fill(int argc, char **argv)
{
    uint32_t addr, len, v;
    if (!need_hex(argc, argv, 1, &addr) || !need_hex(argc, argv, 2, &len) ||
        !need_hex(argc, argv, 3, &v)) return;
    memset((void *)addr, (int)v, len);
}

static void cmd_go(int argc, char **argv)
{
    uint32_t addr = last_entry;
    if (argc > 1 && !need_hex(argc, argv, 1, &addr)) return;
    if (addr & 1) { kprintf("odd address\n"); return; }
    kprintf("go %06x\n", addr);
    con_cursor_show(0);
    run_user(addr);
}

static void cmd_led(int argc, char **argv)
{
    uint32_t v;
    if (!need_hex(argc, argv, 1, &v)) return;
    led_set(0, v & 1);
    led_set(1, v & 2);
}

static void cmd_page(int argc, char **argv)
{
    uint32_t v;
    if (!need_hex(argc, argv, 1, &v)) return;
    if (bios_video_set_page((int)v) < 0) kprintf("page 0 or 1\n");
}

static void cmd_ramtest(void)
{
    /* Destructive: USER_BASE..RAM_END (framebuffers included). SYSDATA holds our stack. */
    static const uint32_t pats[] = { 0x55555555, 0xAAAAAAAA, 0x00000000, 0xFFFFFFFF };
    volatile uint32_t *base = (volatile uint32_t *)USER_BASE;
    uint32_t n = (RAM_END - USER_BASE) / 4, errors = 0;
    kprintf("ramtest %06x-%06x (screen will be lost)\n", (unsigned)USER_BASE, (unsigned)(RAM_END - 1));
    for (uint32_t i = 0; i < n; i++) base[i] = (uint32_t)(uintptr_t)&base[i];
    for (uint32_t i = 0; i < n; i++)
        if (base[i] != (uint32_t)(uintptr_t)&base[i]) {
            if (errors++ < 4) kprintf("\n%06x exp %08x got %08x", (unsigned)(uintptr_t)&base[i],
                                      (unsigned)(uintptr_t)&base[i], base[i]);
        }
    for (int p = 0; p < 4; p++) {
        uint32_t v = pats[p];
        for (uint32_t i = 0; i < n; i++) base[i] = v;
        for (uint32_t i = 0; i < n; i++)
            if (base[i] != v) {
                if (errors++ < 4) kprintf("\n%06x exp %08x got %08x", (unsigned)(uintptr_t)&base[i], v, base[i]);
            }
    }
    con_clear();
    kprintf("ramtest: %s (%u errors)\n", errors ? "FAIL" : "ok", errors);
}

static void cmd_beep(void)
{
    for (int i = 0; i < 200; i++) {         /* 1 kHz for 200 ms */
        gpio_set(GPIO_AUDIO);
        delay_us(500);
        gpio_clear(GPIO_AUDIO);
        delay_us(500);
    }
}

static void cmd_info(void)
{
    print_banner();
    kprintf("ticks %u (%u/s) INTSTAT %04x INTEN %04x\n", ticks, ticks_per_sec, IO_INTSTAT, inten_shadow);
    kprintf("GPIO %04x DUART SRA %02x rx overruns %u ps2 err %u\n", gpio_shadow, DUART_SRA,
            serial_rx_overruns, ps2_errors);
    kprintf("go default %06x\n", last_entry);
}

static void execute(char *line)
{
    char *argv[12];
    int argc = split(line, argv, 12);
    if (!argc) return;
    const char *c = argv[0];
    if (!strcmp(c, "help") || !strcmp(c, "?")) cmd_help();
    else if (!strcmp(c, "md")) cmd_md(argc, argv);
    else if (!strcmp(c, "mb")) cmd_mem_write(argc, argv, 1);
    else if (!strcmp(c, "mw")) cmd_mem_write(argc, argv, 2);
    else if (!strcmp(c, "ml")) cmd_mem_write(argc, argv, 4);
    else if (!strcmp(c, "fill")) cmd_fill(argc, argv);
    else if (!strcmp(c, "go")) cmd_go(argc, argv);
    else if (!strcmp(c, "regs")) exception_print_regs();
    else if (!strcmp(c, "reset")) cpu_reset();
    else if (!strcmp(c, "led")) cmd_led(argc, argv);
    else if (!strcmp(c, "page")) cmd_page(argc, argv);
    else if (!strcmp(c, "ticks")) kprintf("%u\n", ticks);
    else if (!strcmp(c, "ramtest")) cmd_ramtest();
    else if (!strcmp(c, "beep")) cmd_beep();
    else if (!strcmp(c, "cls")) con_clear();
    else if (!strcmp(c, "info")) cmd_info();
    else if (!strcmp(c, "srec")) srec_receive(0);
    else if (!strcmp(c, "dir") || !strcmp(c, "run") || !strcmp(c, "cat") || !strcmp(c, "sd"))
        kprintf("%s: not implemented (stage 2)\n", c);
    else kprintf("unknown command '%s' (try help)\n", c);
}

void monitor_main(void)
{
    static char line[LINE_MAX];
    for (;;) {
        kprintf("> ");
        int n = read_line(line, LINE_MAX);
        if (n > 0) execute(line);
    }
}
