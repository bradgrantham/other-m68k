/* TRAP #15 system calls. The dispatcher is generated from the spec; every bios_* below
 * implements one call. Filesystem calls go through fs.c (FatFs), raw blocks through sd.c,
 * exec through exec.c.
 *
 * User pointers are checked before use: they must lie in RAM above the BIOS area
 * (USER_BASE..RAM_END, which includes both framebuffers so a program may read a file
 * straight into a page). A bad pointer gives E_FAULT. */
#include "bios.h"
#include "sd.h"
#include "fs.h"
#include "exec.h"
#include "args.h"
#include "clock.h"
#include "joy.h"

uint32_t video_standard = VIDEO_STD_NTSC;
uint32_t video_page;

void syscall_dispatch(struct trap_frame *f)
{
    uint32_t ret;
#include "syscall_dispatch.inc"
    f->d0 = ret;
}

/* ---- user pointer validation ---------------------------------------------------- */

static int user_range_ok(const void *p, uint32_t len)
{
    uintptr_t a = (uintptr_t)p;
    return a >= USER_BASE && a < RAM_END && len <= RAM_END - a;
}

/* NUL-terminated string entirely inside user RAM and shorter than max. */
static int user_str_ok(const char *s, uint32_t max)
{
    if (!user_range_ok(s, 1)) return 0;
    uint32_t room = (uint32_t)(RAM_END - (uintptr_t)s);
    if (room > max) room = max;
    for (uint32_t i = 0; i < room; i++)
        if (s[i] == 0) return 1;
    return 0;
}

void user_cleanup(void)
{
    fs_close_all();
    joy_paddles_set(0);                     /* counter/timer and ACR back to the serial defaults */
    audio_stop();                           /* before irq_reset_user: nothing playing lets INTEN.HSYNC drop */
    irq_reset_user();
    bios_video_set_page(0);
    gpio_write((uint16_t)(gpio_shadow & ~(GPIO_LED0 | GPIO_LED1)));
    con_cursor_show(0);
}

void bios_exit(int code)
{
    user_cleanup();
    kprintf("\n[exit %d]\n", code);
    monitor_restart();
}

int bios_putchar(int c)
{
    putchar_both(c);
    return (uint8_t)c;
}

int bios_getchar(void) { return input_getchar(); }
int bios_pollchar(void) { return input_pollchar(); }

/* The string must start in user RAM; printing stops at the NUL or at RAM_END, so an
 * unterminated string cannot walk into the I/O or ROM regions. */
int bios_puts(const char *s)
{
    if (!user_range_ok(s, 1)) return E_FAULT;
    uint32_t room = (uint32_t)(RAM_END - (uintptr_t)s);
    int n = 0;
    while ((uint32_t)n < room && s[n]) putchar_both(s[n++]);
    return n;
}

int bios_serial_putc(int c) { serial_putc(c); return (uint8_t)c; }
int bios_serial_getc(void) { return serial_getc(); }
uint32_t bios_ticks(void) { return ticks; }

void bios_wait_vblank(void)
{
    uint32_t t = ticks;
    while (ticks == t) ;
}

int bios_video_info(struct video_info *info)
{
    if (!user_range_ok(info, sizeof *info)) return E_FAULT;
    info->width = VIDEO_WIDTH;
    info->height = VIDEO_HEIGHT;
    info->bpp = VIDEO_BPP;
    info->bytes_per_line = VIDEO_BYTES_PER_LINE;
    info->page_addr[0] = FB_PAGE0_ADDR;
    info->page_addr[1] = FB_PAGE1_ADDR;
    info->current_page = video_page;
    info->standard = video_standard;
    return 0;
}

int bios_video_set_page(int page)
{
    if (page == 0) SW_PAGE0_SET();
    else if (page == 1) SW_PAGE1_SET();
    else return E_INVAL;
    video_page = (uint32_t)page;
    return 0;
}

int bios_open(int flags, const char *path)
{
    if (!user_str_ok(path, FS_PATH_MAX)) return E_FAULT;
    return fs_open(flags, path);
}

int bios_close(int fd)
{
    if (fd >= 0 && fd < FS_FD_BASE) return 0;       /* console: nothing to do */
    return fs_close(fd);
}

int bios_read(int fd, unsigned len, void *buf)
{
    /* the destination may also be a BIOS audio buffer the program holds (spec rev 8) */
    if (!user_range_ok(buf, len) && !audio_buf_range_ok(buf, len)) return E_FAULT;
    if (fd == 0) {
        uint8_t *p = buf;
        unsigned n = 0;
        if (!len) return 0;
        p[n++] = (uint8_t)input_getchar();          /* block for the first byte only */
        while (n < len) {
            int c = input_pollchar();
            if (c < 0) break;
            p[n++] = (uint8_t)c;
        }
        return (int)n;
    }
    if (fd == 1 || fd == 2) return E_BADF;
    return fs_read(fd, buf, len);
}

int bios_write(int fd, unsigned len, const void *buf)
{
    if (!user_range_ok(buf, len)) return E_FAULT;
    if (fd == 1 || fd == 2) {
        const uint8_t *p = buf;
        for (unsigned i = 0; i < len; i++) putchar_both(p[i]);
        return (int)len;
    }
    if (fd == 0) return E_BADF;
    return fs_write(fd, buf, len);
}

int bios_lseek(int fd, int offset, int whence)
{
    if (fd >= 0 && fd < FS_FD_BASE) return E_INVAL;     /* console is not seekable */
    return fs_lseek(fd, offset, whence);
}

int bios_opendir(const char *path)
{
    if (!user_str_ok(path, FS_PATH_MAX)) return E_FAULT;
    return fs_opendir(path);
}

int bios_readdir(int handle, struct dirent *ent)
{
    if (!user_range_ok(ent, sizeof *ent)) return E_FAULT;
    return fs_readdir(handle, ent);
}

int bios_closedir(int handle) { return fs_closedir(handle); }

int bios_unlink(const char *path)
{
    if (!user_str_ok(path, FS_PATH_MAX)) return E_FAULT;
    return fs_unlink(path);
}

int bios_rename(const char *oldp, const char *newp)
{
    if (!user_str_ok(oldp, FS_PATH_MAX) || !user_str_ok(newp, FS_PATH_MAX)) return E_FAULT;
    return fs_rename(oldp, newp);
}

int bios_mkdir(const char *path)
{
    if (!user_str_ok(path, FS_PATH_MAX)) return E_FAULT;
    return fs_mkdir(path);
}

int bios_chdir(const char *path)
{
    if (!user_str_ok(path, FS_PATH_MAX)) return E_FAULT;
    return fs_chdir(path);
}

int bios_getcwd(unsigned len, char *buf)
{
    if (!len) return E_INVAL;
    if (!user_range_ok(buf, len)) return E_FAULT;
    return fs_getcwd(buf, len);
}

uint32_t bios_time(void) { return clock_get(); }

int bios_joystick(void) { return joy_read(); }

int bios_paddles(struct paddles *p)
{
    if (!p) { joy_paddles_set(0); return 0; }
    if (!user_range_ok(p, sizeof *p)) return E_FAULT;
    if (!joy_paddles_on()) joy_paddles_set(1);
    joy_paddles_get(p);
    return 0;
}
int bios_set_time(uint32_t secs) { clock_set(secs); return 0; }

static int sd_to_abi(int rc)
{
    switch (rc) {
    case SD_OK: return 0;
    case SD_ERR_RANGE: return E_INVAL;
    default: return E_IO;
    }
}

int bios_sd_read_block(uint32_t lba, void *buf)
{
    if (!user_range_ok(buf, SD_BLOCK_SIZE)) return E_FAULT;
    if (!sd_card.ready && sd_init() != SD_OK) return E_IO;
    return sd_to_abi(sd_read_block(lba, buf));
}

int bios_sd_write_block(uint32_t lba, const void *buf)
{
    if (!user_range_ok(buf, SD_BLOCK_SIZE)) return E_FAULT;
    if (!sd_card.ready && sd_init() != SD_OK) return E_IO;
    return sd_to_abi(sd_write_block(lba, buf));
}

/* Chain to another program. The headers are checked before anything is disturbed, so
 * a missing or unloadable file is reported to the caller; once loading has started the
 * caller is gone, and a load error (I/O) ends in the monitor. */
int bios_exec(const char *path, const char *args)
{
    static struct exec_image img;
    static char args_copy[ARGS_MAX_BYTES];
    int rc;
    if (!user_str_ok(path, FS_PATH_MAX)) return E_FAULT;
    if (args && !user_str_ok(args, sizeof args_copy)) return E_FAULT;
    rc = exec_prepare(path, &img);
    if (rc) return rc;
    if (args) strcpy(args_copy, args); else args_copy[0] = 0;
    user_cleanup();
    rc = exec_load(&img);
    if (rc) {
        kprintf("\nexec %s: %s\n", img.path, fs_strerror(rc));
        monitor_restart();
    }
    con_cursor_show(0);
    user_launch(img.entry, img.path, args_copy);
}

void *bios_set_irq_handler(int level, void *handler)
{
    if (level < 1 || level > 4) return (void *)(intptr_t)E_INVAL;
    uint16_t sr = irq_disable();
    void *old = (void *)irq_user_hook[level];
    irq_user_hook[level] = (irq_hook_fn)handler;
    irq_update_inten();
    irq_restore(sr);
    return old;
}

int bios_mem_info(struct mem_info *info)
{
    if (!user_range_ok(info, sizeof *info)) return E_FAULT;
    info->user_start = USER_BASE;
    info->user_end = USER_END;
    info->user_end_without_fb1 = FB1_END;
    return 0;
}

int bios_con_set_cursor(int x, int y)
{
    if (x < 0 || x >= CON_COLS || y < 0 || y >= CON_ROWS) return E_INVAL;
    con_set_cursor(x, y);
    return 0;
}

int bios_con_clear(void) { con_clear(); return 0; }

int bios_con_info(struct con_info *info)
{
    int x, y;
    if (!user_range_ok(info, sizeof *info)) return E_FAULT;
    con_get_cursor(&x, &y);
    info->cols = CON_COLS;
    info->rows = CON_ROWS;
    info->cursor_x = (uint32_t)x;
    info->cursor_y = (uint32_t)y;
    return 0;
}

int bios_key_event(void) { return input_key_event(); }

void bios_sleep_ms(uint32_t ms)
{
    /* Whole frames by the VBLANK counter, the fraction of a frame by a calibrated loop.
     * frames = floor(ms * tps / 1000), split into seconds and milliseconds so the product
     * cannot overflow 32 bits for any ms; the leftover (ms * tps) mod 1000 is in units of
     * 1/(1000 * tps) s, i.e. rest * 1000 / tps microseconds (< one frame). */
    uint32_t tps = ticks_per_sec;
    uint32_t sub = (ms % 1000) * tps;                  /* < 1000 * 60 */
    uint32_t frames = (ms / 1000) * tps + sub / 1000;
    if (frames) {
        uint32_t t0 = ticks;
        while ((uint32_t)(ticks - t0) < frames) ;
    }
    delay_us((sub % 1000) * 1000 / tps);
}

int bios_set_audio_cb(void *cb)
{
    uint16_t sr = irq_disable();
    audio_cb = (irq_hook_fn)cb;
    irq_update_inten();                     /* also picks the fast or slow level-4 handler */
    irq_restore(sr);
    return 0;
}

/* Spec rev 6: PDM pattern streaming by buffer hand-off (audio.c). The buffers are BIOS
 * memory, so there is nothing to range-check: audio_next/last accept only their own. */
uint8_t *bios_audio_start(unsigned bufsize)
{
    return audio_start(bufsize);
}

uint8_t *bios_audio_next(const uint8_t *filled, int block)
{
    return audio_next(filled, block);
}

int bios_audio_last(const uint8_t *filled, int block)
{
    return audio_last(filled, block);
}
