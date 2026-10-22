/* Machine monitor: line editor, S-record upload (`srec`), memory commands, two-volume file
 * management (C: flash, D: card), scripts (.CMD) and user program launch with a DOS-like
 * search path (spec "filesystem"). */
#include "bios.h"
#include "sd.h"
#include "fs.h"
#include "exec.h"
#include "flash.h"
#include "flashdisk.h"
#include "clock.h"
#include "args.h"
#include "lineedit.h"

#define LINE_MAX 128
#define SREC_LINE_MAX 520
#define SCRIPT_DEPTH 4

static char cur_drive = 'C';

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

/* S-records arrive on serial port A, which is not console input after the POST: read it
 * directly. Returns the line length, or -1 if ESC was pressed on the keyboard or received
 * on the port. Nothing is echoed while records arrive: at 38400 baud a record line lands
 * every ~11 ms and echoing it (console glyphs, a 35 ms scroll) would take longer than
 * that, so the 512-byte RX ring would overflow within a few lines. The DUART's 3-byte
 * FIFO is drained by the level-3 ISR, which keeps running during the parse. */
static int read_serial_line(char *buf, int max)
{
    int n = 0, cr = 0;
    for (;;) {
        int c;
        while ((c = serial_getc()) < 0)
            if (input_pollchar() == 0x1B) return -1;
        if (c == '\n' && cr) { cr = 0; continue; }
        cr = (c == '\r');
        if (c == 0x1B) return -1;
        if (c == '\r' || c == '\n') { buf[n] = 0; return n; }
        if (n < max - 1) buf[n++] = (char)c;
    }
}

static void srec_receive(void)
{
    static char line[SREC_LINE_MAX];
    struct srec_state s;
    srec_init(&s, srec_store, 0);
    srec_rejected = 0;
    while (serial_getc() >= 0) ;                        /* stale bytes */
    kprintf("send S-records on serial A (ESC aborts)\n");
    int n = read_serial_line(line, SREC_LINE_MAX);
    if (n < 0) { kprintf("aborted\n"); return; }
    for (;;) {
        if (n > 0) {
            if (line[0] != 'S' && line[0] != 's') break;   /* a non-record line ends the upload */
            if (srec_line(&s, line) == 1) break;
        }
        n = read_serial_line(line, SREC_LINE_MAX);
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

static int need_hex(int argc, char **argv, int i, uint32_t *v)
{
    if (i >= argc || !parse_hex(argv[i], v)) { kprintf("bad or missing hex argument\n"); return 0; }
    return 1;
}

/* ---- commands ----------------------------------------------------------------------- */

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
    user_launch(addr, 0, 0);
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
    /* 1 kHz for 200 ms: all-ones and all-zeros patterns straight into the PDM shifter
     * (rev 5), which repeats a pattern until it is replaced, so this is the old 1-bit
     * square wave. Back to the idle (mid-level) pattern afterwards. */
    for (int i = 0; i < 200; i++) {
        IO_AUDIO = 0xFF;
        delay_us(500);
        IO_AUDIO = 0x00;
        delay_us(500);
    }
    IO_AUDIO = AUDIO_IDLE_PATTERN;
}

static void cmd_info(void)
{
    print_banner();
    kprintf("ticks %u (%u/s) INTSTAT %04x INTEN %04x\n", ticks, ticks_per_sec, IO_INTSTAT, inten_shadow);
    kprintf("GPIO %04x DUART SRA %02x rx overruns %u ps2 err %u\n", gpio_shadow, DUART_SRA,
            serial_rx_overruns, ps2_errors);
    kprintf("go default %06x\n", last_entry);
    char cwd[FS_PATH_MAX], d[20];
    if (fs_getcwd(cwd, sizeof cwd) < 0) strcpy(cwd, "?");
    if (clock_get()) clock_format(clock_get(), d); else strcpy(d, "not set");
    kprintf("cwd %s  clock %s\n", cwd, d);
    kprintf("C: %u erase/program cycles since boot%s\n", flashdisk_programs, flashdisk_dirty() ? ", cache dirty" : "");
}

/* ---- SD card and files ------------------------------------------------------------- */

static uint8_t blockbuf[SD_BLOCK_SIZE];

static void cmd_sd(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "rd")) {
        uint32_t lba;
        if (!need_hex(argc, argv, 2, &lba)) return;
        if (!sd_card.ready) {
            int rc = sd_init();
            if (rc) { kprintf("sd: %s\n", sd_strerror(rc)); return; }
        }
        int rc = sd_read_block(lba, blockbuf);
        if (rc) { kprintf("sd rd %x: %s\n", lba, sd_strerror(rc)); return; }
        char line[48];
        for (int off = 0; off < SD_BLOCK_SIZE; off += HEXDUMP_BYTES_PER_LINE) {
            hexdump_line(line, (uint32_t)off, blockbuf + off, HEXDUMP_BYTES_PER_LINE);
            kprintf("%s\n", line);             /* not key-abortable: must not eat typed-ahead input */
        }
        return;
    }
    /* (re)initialise and describe the card; the filesystem is remounted on next use */
    fs_invalidate();
    int rc = sd_init();
    if (rc) { kprintf("sd: %s\n", sd_strerror(rc)); return; }
    const uint8_t *cid = sd_card.cid;
    kprintf("%s, %d-bit bus, RCA %04x\n", sd_type_name(sd_card.type), sd_card.bus4 ? 4 : 1, sd_card.rca);
    kprintf("%u blocks = %u MB\n", sd_card.blocks, sd_card.blocks / 2048);
    kprintf("CID mid %02x oid %c%c pnm %c%c%c%c%c rev %u.%u\n", cid[0], cid[1], cid[2], cid[3],
            cid[4], cid[5], cid[6], cid[7], cid[8] >> 4, cid[8] & 15);
    kprintf("sn %02x%02x%02x%02x mdt %u/%u\n", cid[9], cid[10], cid[11], cid[12],
            2000 + (((cid[13] & 0x0F) << 4) | (cid[14] >> 4)), cid[14] & 0x0F);
    kprintf("CSD v%u", (sd_card.csd[0] >> 6) + 1);
    for (int i = 0; i < 16; i++) kprintf("%s%02x", i ? "" : " ", sd_card.csd[i]);
    kprintf("\n");
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2) { kprintf("cat file\n"); return; }
    int fd = fs_open(O_RDONLY, argv[1]);
    if (fd < 0) { kprintf("cat %s: %s\n", argv[1], fs_strerror(fd)); return; }
    int n;
    uint32_t total = 0;
    while ((n = fs_read(fd, blockbuf, sizeof blockbuf)) > 0) {
        for (int i = 0; i < n; i++) if (blockbuf[i] != '\r') putchar_both(blockbuf[i]);
        total += (uint32_t)n;
        if (input_has_esc()) { input_flush(); break; }      /* ESC stops a long file */
    }
    if (n < 0) kprintf("\ncat: %s\n", fs_strerror(n));
    fs_close(fd);
}

static void cmd_flashid(void)
{
    uint16_t mfr, dev;
    int ok = flash_read_id(&mfr, &dev);
    kprintf("flash ID %04x/%04x (even chip %02x %02x, odd chip %02x %02x): %s\n", mfr, dev,
            mfr >> 8, dev >> 8, mfr & 0xFF, dev & 0xFF,
            ok ? "SST39SF040 x2" : "not the expected SST39SF040 pair");
}

static void cmd_flash(int argc, char **argv)
{
    if (argc < 2) { kprintf("flash file   (a %uK rom.bin from fw/mkrom.py on the SD card)\n", (unsigned)(ROM_IMAGE_SIZE / 1024)); return; }
    flash_update(argv[1]);
}


/* ---- names and wildcards -------------------------------------------------------------- */

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int is_dir_path(const char *path, int *exists)
{
    struct dirent e;
    size_t n = strlen(path);
    *exists = 0;
    if (!n) return 1;
    if (path[n - 1] == '/' || path[n - 1] == '\\' || path[n - 1] == ':') return 1;
    if (fs_stat(path, &e) == 0) { *exists = 1; return (e.attr & ATTR_DIR) != 0; }
    return 0;
}

/* ---- files and directories ------------------------------------------------------------- */

static void fmt_stamp(uint32_t mtime, char *out)       /* "yy-mm-dd hh:mm" or blanks */
{
    if (!mtime) { strcpy(out, "              "); return; }
    struct clock_tm t;
    clock_to_tm(mtime, &t);
    ksnprintf(out, 16, "%02d-%02d-%02d %02d:%02d", t.year % 100, t.mon, t.day, t.hour, t.min);
}

static void cmd_dir(int argc, char **argv)
{
    char dir[FS_PATH_MAX];
    const char *pat = "*";
    if (argc > 1) {
        int exists;
        if (has_wild(argv[1]) || !is_dir_path(argv[1], &exists)) split_path(argv[1], dir, &pat);
        else strcpy(dir, argv[1]);
    } else dir[0] = 0;
    int h = fs_opendir(dir_for_open(dir));
    if (h < 0) { kprintf("dir %s: %s\n", dir[0] ? dir : ".", fs_strerror(h)); return; }
    struct dirent ent;
    int n = 0, rc;
    uint32_t bytes = 0;
    char stamp[16];
    while ((rc = fs_readdir(h, &ent)) > 0) {
        if (!glob_match(pat, ent.name)) continue;
        fmt_stamp(ent.mtime, stamp);
        if (strlen(ent.name) > 16) kprintf("%s\n%16s", ent.name, "");
        else kprintf("%-16s", ent.name);
        if (ent.attr & ATTR_DIR) kprintf("   <DIR> %s\n", stamp);
        else { kprintf("%8u %s\n", ent.size, stamp); bytes += ent.size; }
        n++;
        if (input_has_esc()) { input_flush(); break; }      /* ESC stops a long listing */
    }
    fs_closedir(h);
    if (rc < 0) kprintf("dir: %s\n", fs_strerror(rc));
    else kprintf("%d entries, %u bytes\n", n, bytes);
}

static void cmd_cd(int argc, char **argv)
{
    if (argc < 2) {
        char cwd[FS_PATH_MAX];
        int rc = fs_getcwd(cwd, sizeof cwd);
        if (rc < 0) kprintf("cd: %s\n", fs_strerror(rc)); else kprintf("%s\n", cwd);
        return;
    }
    int rc = fs_chdir(argv[1]);
    if (rc) kprintf("cd %s: %s\n", argv[1], fs_strerror(rc));
    else if (argv[1][0] && argv[1][1] == ':' && argv[1][2] == 0) cur_drive = (char)(lower(argv[1][0]) - 32);
}

static void cmd_drive(const char *d)
{
    char name[3] = { (char)(lower(d[0]) - 32), ':', 0 };
    int rc = fs_chdir(name);
    if (rc) kprintf("%s: %s\n", name, fs_strerror(rc));
    else cur_drive = name[0];
}

/* For each file matching `spec` (a path whose last component may hold wildcards) call
 * fn(dir, name, ctx). Returns the number of matches or E_*. */
static int for_each_match(const char *spec, int (*fn)(const char *dir, const char *name, void *ctx), void *ctx)
{
    char dir[FS_PATH_MAX], opendir_name[FS_PATH_MAX];
    const char *pat;
    split_path(spec, dir, &pat);
    strcpy(opendir_name, dir);
    int h = fs_opendir(dir_for_open(opendir_name));
    if (h < 0) return h;
    struct dirent ent;
    int n = 0, rc;
    while ((rc = fs_readdir(h, &ent)) > 0) {
        if ((ent.attr & ATTR_DIR) || !glob_match(pat, ent.name)) continue;
        n++;
        if (fn(dir, ent.name, ctx)) { rc = -1; break; }
    }
    fs_closedir(h);
    return rc < 0 ? (rc == -1 ? n : rc) : n;
}

static uint8_t blockbuf2[SD_BLOCK_SIZE];        /* copy transfer buffer (blockbuf is for sd rd/cat) */

struct copy_ctx { const char *dst; int dst_is_dir; int errors; };

static int copy_one(const char *dir, const char *name, void *vctx)
{
    struct copy_ctx *c = vctx;
    char src[FS_PATH_MAX], dst[FS_PATH_MAX];
    if (!join_path(src, sizeof src, dir, name) || !(c->dst_is_dir ? join_path(dst, sizeof dst, c->dst, name) : (strcpy(dst, c->dst), 1))) {
        kprintf("copy: path too long\n"); c->errors++; return 1;
    }
    kprintf("%s -> %s", src, dst);
    int rc = fs_copy(src, dst, blockbuf2, sizeof blockbuf2);
    if (rc < 0) { kprintf(": %s\n", fs_strerror(rc)); c->errors++; }
    else kprintf("  %d bytes\n", rc);
    if (input_has_esc()) { input_flush(); return 1; }       /* ESC stops a long copy */
    return 0;
}

static void cmd_copy(int argc, char **argv)
{
    if (argc < 3) { kprintf("copy src dst   (src may hold * and ?; dst a file, a directory or a drive)\n"); return; }
    struct copy_ctx c;
    int exists;
    c.dst = argv[2];
    c.dst_is_dir = is_dir_path(argv[2], &exists);
    c.errors = 0;
    if (has_wild(argv[1]) && !c.dst_is_dir) { kprintf("copy: with wildcards dst must be a directory\n"); return; }
    if (!has_wild(argv[1]) && !c.dst_is_dir) {
        struct dirent e;
        int rc = fs_stat(argv[1], &e);
        if (rc == 0 && (e.attr & ATTR_DIR)) { kprintf("copy: %s is a directory\n", argv[1]); return; }
        rc = copy_one("", argv[1], &c);
        (void)rc;
        return;
    }
    int n = for_each_match(argv[1], copy_one, &c);
    if (n < 0) kprintf("copy %s: %s\n", argv[1], fs_strerror(n));
    else if (n == 0) kprintf("copy: no match for %s\n", argv[1]);
    else kprintf("%d file%s, %d error%s\n", n, n == 1 ? "" : "s", c.errors, c.errors == 1 ? "" : "s");
}

static int del_one(const char *dir, const char *name, void *ctx)
{
    (void)ctx;
    char path[FS_PATH_MAX];
    if (!join_path(path, sizeof path, dir, name)) return 0;
    int rc = fs_unlink(path);
    if (rc) kprintf("del %s: %s\n", path, fs_strerror(rc));
    return 0;
}

static void cmd_del(int argc, char **argv)
{
    if (argc < 2) { kprintf("del file   (wildcards ok; rmdir for directories)\n"); return; }
    if (has_wild(argv[1])) {
        int n = for_each_match(argv[1], del_one, 0);
        if (n < 0) kprintf("del %s: %s\n", argv[1], fs_strerror(n));
        else kprintf("%d file%s\n", n, n == 1 ? "" : "s");
        return;
    }
    struct dirent e;
    int rc = fs_stat(argv[1], &e);
    if (rc == 0 && (e.attr & ATTR_DIR)) { kprintf("del: %s is a directory (rmdir)\n", argv[1]); return; }
    if (rc == 0) rc = fs_unlink(argv[1]);
    if (rc) kprintf("del %s: %s\n", argv[1], fs_strerror(rc));
}

static void cmd_rmdir(int argc, char **argv)
{
    if (argc < 2) { kprintf("rmdir dir   (must be empty)\n"); return; }
    struct dirent e;
    int rc = fs_stat(argv[1], &e);
    if (rc == 0 && !(e.attr & ATTR_DIR)) { kprintf("rmdir: %s is not a directory\n", argv[1]); return; }
    if (rc == 0) rc = fs_unlink(argv[1]);
    if (rc) kprintf("rmdir %s: %s\n", argv[1], fs_strerror(rc));
}

static void cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) { kprintf("mkdir dir\n"); return; }
    int rc = fs_mkdir(argv[1]);
    if (rc) kprintf("mkdir %s: %s\n", argv[1], fs_strerror(rc));
}

static void cmd_ren(int argc, char **argv)
{
    if (argc < 3) { kprintf("ren old new   (new may be another directory on the same drive)\n"); return; }
    int rc = fs_rename(argv[1], argv[2]);
    if (rc) kprintf("ren %s: %s\n", argv[1], fs_strerror(rc));
}

static void cmd_format(int argc, char **argv)
{
    if (argc < 2 || !((argv[1][0] | 0x20) == 'c' && argv[1][1] == ':' && argv[1][2] == 0)) {
        kprintf("format C:   (erase and recreate the flash volume; D: is not supported)\n");
        return;
    }
    kprintf("Erase everything on C: and create an empty volume? (y/N) ");
    int c = input_getchar();
    kprintf("%c\n", (c >= ' ' && c < 127) ? c : ' ');
    if (c != 'y' && c != 'Y') { kprintf("format: cancelled\n"); return; }
    kprintf("formatting (interrupts off while each sector is written)...\n");
    int rc = fs_mkfs("C:", blockbuf2, sizeof blockbuf2);
    if (rc) kprintf("format: %s\n", fs_strerror(rc));
    else kprintf("C: formatted\n");
}

/* ---- clock ------------------------------------------------------------------------------ */

static void set_date(const char *text)
{
    uint32_t secs;
    if (!clock_parse(text, &secs)) { kprintf("date: use YYYY-MM-DD HH:MM[:SS]\n"); return; }
    clock_set(secs);
}

static void cmd_date(const char *rest)
{
    char d[20];
    while (*rest == ' ') rest++;
    if (*rest) { set_date(rest); return; }
    if (clock_get()) { clock_format(clock_get(), d); kprintf("%s\n", d); }
    else kprintf("clock not set\n");
    static char line[32];
    int n = lineedit_read("new date and time (YYYY-MM-DD HH:MM, Enter keeps): ", line, sizeof line, 0, 0);
    if (n > 0) set_date(line);
}

/* ---- scripts ------------------------------------------------------------------------------ */

/* A .CMD file's lines are executed as typed. Running a program does not return to this
 * code (the program's exit lands in monitor_restart), so a script's position is kept in
 * static state (path and byte offset) and the file is reopened for every line. */
struct script { char path[FS_PATH_MAX]; uint32_t offset; };
static struct script scripts[SCRIPT_DEPTH];
static int script_depth;

static int script_start(const char *path)
{
    if (script_depth == SCRIPT_DEPTH) { kprintf("%s: scripts nested too deep\n", path); return -1; }
    if (strlen(path) >= FS_PATH_MAX) return -1;
    strcpy(scripts[script_depth].path, path);
    scripts[script_depth].offset = 0;
    script_depth++;
    return 0;
}

/* Next line of the innermost script into buf; its length, or -1 when the script has ended
 * (it is popped). CR, LF and CRLF end a line. */
static int script_line(char *buf, int max)
{
    struct script *s = &scripts[script_depth - 1];
    int fd = fs_open(O_RDONLY, s->path);
    int n = -1;
    if (fd >= 0 && fs_lseek(fd, (int)s->offset, SEEK_SET) >= 0) n = fs_read(fd, buf, (unsigned)max - 1);
    if (fd >= 0) fs_close(fd);
    if (n <= 0) {
        if (n < 0) kprintf("%s: %s\n", s->path, fs_strerror(n));
        script_depth--;
        return -1;
    }
    int len = 0;
    while (len < n && buf[len] != '\n' && buf[len] != '\r') len++;
    int consumed = len;
    if (consumed < n && buf[consumed] == '\r') consumed++;
    if (consumed < n && buf[consumed] == '\n') consumed++;
    if (consumed == len && len == n && n == max - 1) { kprintf("%s: line too long\n", s->path); }
    s->offset += (uint32_t)consumed;
    buf[len] = 0;
    return len;
}

static void cmd_exit(void)
{
    if (script_depth) script_depth--;
    else kprintf("exit: not in a script\n");
}

/* ---- programs ----------------------------------------------------------------------------- */

/* Load `path` and run it with the raw argument string `args`.  Returns the loader's error
 * (E_NOENT if there is no such file) without printing when `quiet_noent` is set, so the
 * caller can fall back to "unknown command". */
static int run_file(const char *path, char *args, int quiet_noent)
{
    static struct exec_image img;
    static char expanded[ARGS_MAX_BYTES];
    int rc = args_expand(args, expanded, sizeof expanded);     /* * and ? against the filesystem */
    if (rc < 0) { kprintf("run %s: too many arguments after expansion (%d words, %d bytes at most)\n", path, ARGS_MAX_WORDS, ARGS_MAX_BYTES - 1); return rc; }
    args = expanded;
    rc = exec_prepare(path, &img);
    if (rc == 0) rc = exec_load(&img);
    if (rc) {
        if (!(quiet_noent && rc == E_NOENT)) kprintf("run %s: %s\n", path, fs_strerror(rc));
        return rc;
    }
    kprintf("run %s: %06x-%06x entry %06x\n", img.path, img.lowest, img.highest - 1, img.entry);
    last_entry = img.entry;
    con_cursor_show(0);
    fs_sync();
    user_launch(img.entry, img.path, args);
    return 0;   /* not reached: the program returns through monitor_restart */
}

/* Split `rest` (the raw line after the command word) into the first word and the rest. */
static char *first_word(char *rest, char **args)
{
    while (*rest == ' ' || *rest == '\t') rest++;
    char *a = rest;
    while (*a && *a != ' ' && *a != '\t') a++;
    if (*a) *a++ = 0;
    while (*a == ' ' || *a == '\t') a++;
    *args = a;
    return rest;
}

/* `run file [args...]`: rest is the raw remainder of the line after the command word. */
static void cmd_run(char *rest)
{
    char *args;
    char *path = first_word(rest, &args);
    if (!*path) { kprintf("run file [args]\n"); return; }
    run_file(path, args, 0);
}

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    if (n < m) return 0;
    for (size_t i = 0; i < m; i++) if (lower(s[n - m + i]) != lower(ext[i])) return 0;
    return 1;
}

/* The search path (spec filesystem.search_path): WORD, WORD.ELF and WORD.CMD in the
 * current directory, then C:/, then D:/; a word with a drive or directory in it is only
 * tried where it says. Fills `out` with the first existing file; 1 found, 0 not. */
static int resolve_command(const char *word, char *out)
{
    static const char *const exts[] = { "", ".ELF", ".CMD" };
    char cwd[FS_PATH_MAX];
    const char *dirs[3] = { "", "C:/", "D:/" };
    int ndirs = 3;
    if (strchr(word, ':')) ndirs = 1;                   /* a drive given: only there */
    else if (fs_getcwd(cwd, sizeof cwd) >= 0) {
        if (word[0] == '/' || word[0] == '\\') { cwd[2] = 0; ndirs = 1; }      /* absolute on the current drive */
        else if (strchr(word, '/') || strchr(word, '\\')) ndirs = 1;
        dirs[0] = cwd;              /* the current directory, as an absolute path: a script keeps
                                       working after it changes the directory or the drive */
    }
    struct dirent e;
    for (int d = 0; d < ndirs; d++)
        for (int x = 0; x < 3; x++) {
            if (!join_path(out, FS_PATH_MAX, dirs[d], word)) continue;
            if (strlen(out) + strlen(exts[x]) >= FS_PATH_MAX) continue;
            strcpy(out + strlen(out), exts[x]);
            if (fs_stat(out, &e) == 0 && !(e.attr & ATTR_DIR)) return 1;
        }
    return 0;
}

/* An unknown command word NAME runs NAME.ELF (a program) or NAME.CMD (a script) found on
 * the search path: `bmpview adobe2.bmp` is `run C:/BMPVIEW.ELF adobe2.bmp` when the
 * program lives on C:. */
static void run_unknown(char *word, char *args)
{
    static char path[FS_PATH_MAX];
    if (!resolve_command(word, path)) {
        kprintf("unknown command '%s' (try help; or %s.ELF / .CMD in ., C:/, D:/)\n", word, word);
        return;
    }
    if (ends_with(path, ".cmd")) { script_start(path); return; }
    run_file(path, args, 0);
}

/* ---- dispatch -------------------------------------------------------------------------------- */

static void cmd_help(void)
{
    kprintf("help           this list (hex args)\n"
            "md [a] [len]   dump memory\n"
            "mb/mw/ml a v.. write bytes/words/longs\n"
            "fill a len v   fill bytes\n"
            "go [a]         run in user mode\n"
            "regs           last exception\n"
            "srec           S-records from serial A\n"
            "reset led n  page n  ticks  cls\n"
            "ramtest  beep  info  flashid\n"
            "sd [rd lba]    card info / dump block\n"
            "C:  D:  cd [path]   drive / directory\n"
            "dir [path|pat] cat file  echo text\n"
            "copy src dst   (wildcards ok)\n"
            "del ren mkdir rmdir   file management\n"
            "date [Y-M-D H:M]  show/set the clock\n"
            "format C:      empty flash volume\n"
            "run file [args] load ELF and run\n"
            "name [args]    NAME.ELF or NAME.CMD\n"
            "               from ., C:/, D:/\n"
            "exit  history  end a script / typed lines\n"
            "flash file     reprogram the ROM\n");
}

static void execute(char *line)
{
    static char raw[LINE_MAX];
    char *argv[12];
    strcpy(raw, line);                  /* split() cuts `line` into words; raw keeps the arguments intact */
    char *rest;
    char *word = first_word(raw, &rest);
    if (!*word) return;
    if (!strcmp(word, "run")) { cmd_run(rest); return; }
    if (!strcmp(word, "echo")) { kprintf("%s\n", rest); return; }
    if (!strcmp(word, "date")) { cmd_date(rest); return; }
    if (!strcmp(word, "rem") || word[0] == '#' || word[0] == ';') return;
    int argc = args_split(line, argv, 0, 12);
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
    else if (!strcmp(c, "srec")) srec_receive();
    else if (!strcmp(c, "sd")) cmd_sd(argc, argv);
    else if (!strcmp(c, "dir") || !strcmp(c, "ls")) cmd_dir(argc, argv);
    else if (!strcmp(c, "cat") || !strcmp(c, "type")) cmd_cat(argc, argv);
    else if (!strcmp(c, "cd")) cmd_cd(argc, argv);
    else if (!strcmp(c, "copy") || !strcmp(c, "cp")) cmd_copy(argc, argv);
    else if (!strcmp(c, "del") || !strcmp(c, "rm")) cmd_del(argc, argv);
    else if (!strcmp(c, "ren") || !strcmp(c, "mv")) cmd_ren(argc, argv);
    else if (!strcmp(c, "mkdir")) cmd_mkdir(argc, argv);
    else if (!strcmp(c, "rmdir")) cmd_rmdir(argc, argv);
    else if (!strcmp(c, "format")) cmd_format(argc, argv);
    else if (!strcmp(c, "exit")) cmd_exit();
    else if (!strcmp(c, "history")) lineedit_history_print();
    else if (!strcmp(c, "flashid")) cmd_flashid();
    else if (!strcmp(c, "flash")) cmd_flash(argc, argv);
    else if (!strcmp(c, "reboot")) cpu_reboot();
    else if (strlen(c) == 2 && c[1] == ':' && ((lower(c[0]) >= 'a' && lower(c[0]) <= 'z'))) cmd_drive(c);
    else run_unknown(word, rest);
}

static void make_prompt(char *out)             /* FS_PATH_MAX + 8 bytes */
{
    if (fs_getcwd(out, FS_PATH_MAX) >= 0) strcpy(out + strlen(out), "> ");
    else ksnprintf(out, 8, "%c:?> ", cur_drive);
}

/* ---- Tab completion ---------------------------------------------------------------------- */

static const char *const builtins[] = {
    "help", "md", "mb", "mw", "ml", "fill", "go", "regs", "reset", "led", "page", "ticks", "ramtest",
    "beep", "cls", "info", "srec", "sd", "dir", "ls", "cat", "type", "cd", "copy", "cp", "del", "rm",
    "ren", "mv", "mkdir", "rmdir", "format", "exit", "history", "flashid", "flash", "reboot", "run",
    "echo", "date", 0 };

static int prefix_ci(const char *prefix, const char *s)
{
    for (; *prefix; prefix++, s++) if (lower(*prefix) != lower(*s)) return 0;
    return 1;
}

struct comp { const char *word; char *out; int outsz; int n; int list, col; int is_dir_last; };

/* one candidate: `name` completes the word's last component; `dir` is the typed prefix before it */
static void candidate(struct comp *c, const char *dir, const char *name, int is_dir)
{
    if (c->list) {
        int w = (int)strlen(name) + (is_dir ? 1 : 0);
        if (c->col && c->col + 2 + w > CON_COLS) { kprintf("\n"); c->col = 0; }
        if (c->col) { kprintf("  "); c->col += 2; }
        kprintf("%s%s", name, is_dir ? "/" : "");
        c->col += w;
        c->n++;
        return;
    }
    char full[FS_PATH_MAX];
    if (!join_path(full, sizeof full, dir, name)) return;
    if (is_dir && strlen(full) + 1 < sizeof full) strcpy(full + strlen(full), "/");
    if (c->n == 0) { ksnprintf(c->out, (size_t)c->outsz, "%s", full); c->is_dir_last = is_dir; }
    else {                                      /* keep the common prefix (case from the first match) */
        int i = 0;
        while (c->out[i] && full[i] && lower(c->out[i]) == lower(full[i])) i++;
        c->out[i] = 0;
    }
    c->n++;
}

static int monitor_complete(const char *word, int first, char *out, int outsz, int *unique, int list)
{
    struct comp c = { word, out, outsz, 0, list, 0, 0 };
    out[0] = 0;
    char dir[FS_PATH_MAX], opendir_name[FS_PATH_MAX];
    const char *name;
    split_path(word, dir, &name);
    if (first && !dir[0]) {
        for (int i = 0; builtins[i]; i++) if (prefix_ci(word, builtins[i])) candidate(&c, "", builtins[i], 0);
        /* programs and scripts along the search path; a name an earlier directory also has
         * is shadowed (that is what the search would run), so it is listed once */
        static const char *const dirs[] = { "", "C:/", "D:/" };
        char cwd[FS_PATH_MAX];
        if (fs_getcwd(cwd, sizeof cwd) < 0) cwd[0] = 0;
        for (int d = 0; d < 3; d++) {
            if (d > 0 && !strcmp(cwd, dirs[d])) continue;          /* the current directory is this root */
            strcpy(opendir_name, dirs[d]);
            int h = fs_opendir(dir_for_open(opendir_name));
            if (h < 0) continue;
            struct dirent e;
            while (fs_readdir(h, &e) > 0) {
                size_t n = strlen(e.name);
                if ((e.attr & ATTR_DIR) || n < 5 || !(prefix_ci(".elf", e.name + n - 4) || prefix_ci(".cmd", e.name + n - 4))) continue;
                if (!prefix_ci(word, e.name) || strlen(word) > n - 4) continue;
                int shadowed = 0;
                for (int k = 0; k < d && !shadowed; k++) {
                    char full[FS_PATH_MAX];
                    if (k > 0 && !strcmp(cwd, dirs[k])) continue;
                    shadowed = join_path(full, sizeof full, dirs[k], e.name) && fs_stat(full, 0) == 0;
                }
                if (shadowed) continue;
                e.name[n - 4] = 0;
                candidate(&c, "", e.name, 0);
            }
            fs_closedir(h);
        }
    } else {
        strcpy(opendir_name, dir);
        int h = fs_opendir(dir_for_open(opendir_name));
        if (h >= 0) {
            struct dirent e;
            while (fs_readdir(h, &e) > 0)
                if (prefix_ci(name, e.name)) candidate(&c, dir, e.name, (e.attr & ATTR_DIR) != 0);
            fs_closedir(h);
        }
    }
    if (list && c.col) kprintf("\n");
    *unique = c.n == 1;
    return c.n;
}

/* At the first prompt after reset: C:/AUTOEXEC.CMD, unless ESC was pressed during the POST
 * (any ESC waiting in the keyboard queue). */
static void autoexec(void)
{
    if (input_has_esc()) { input_flush(); kprintf("AUTOEXEC.CMD skipped\n"); return; }
    if (fs_stat("C:/AUTOEXEC.CMD", 0) == 0) script_start("C:/AUTOEXEC.CMD");
}

void monitor_main(void)
{
    static char line[LINE_MAX];
    static int booted;
    if (!booted) { booted = 1; autoexec(); }
    static char prompt[FS_PATH_MAX + 8];
    for (;;) {
        fs_sync();                          /* nothing stays in the flash write cache across a prompt */
        int n;
        if (script_depth) {
            if (input_has_esc()) { input_flush(); script_depth = 0; kprintf("script aborted\n"); continue; }
            n = script_line(line, LINE_MAX);         /* not echoed: the screen is 40 columns */
        } else {
            make_prompt(prompt);
            n = lineedit_read(prompt, line, LINE_MAX, monitor_complete, 1);
        }
        if (n > 0) execute(line);
    }
}
