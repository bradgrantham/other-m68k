/* TRAP #15 system calls. The dispatcher is generated from the spec; every bios_* below
 * implements one call. Filesystem, SD and exec are stage 2 and return E_NOSYS. */
#include "bios.h"

uint32_t video_standard = VIDEO_STD_NTSC;
uint32_t video_page;

void syscall_dispatch(struct trap_frame *f)
{
    uint32_t ret;
#include "syscall_dispatch.inc"
    f->d0 = ret;
}

void user_cleanup(void)
{
    irq_reset_user();
    bios_video_set_page(0);
    gpio_write((uint16_t)(gpio_shadow & ~(GPIO_LED0 | GPIO_LED1 | GPIO_AUDIO)));
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

int bios_puts(const char *s)
{
    int n = 0;
    while (*s) { putchar_both(*s++); n++; }
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
    if (!info) return E_INVAL;
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

int bios_open(int flags, const char *path) { (void)flags; (void)path; return E_NOSYS; }
int bios_close(int fd) { (void)fd; return E_NOSYS; }

int bios_read(int fd, unsigned len, void *buf)
{
    if (fd != 0) return fd < 3 ? E_BADF : E_NOSYS;
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

int bios_write(int fd, unsigned len, const void *buf)
{
    if (fd != 1 && fd != 2) return fd == 0 ? E_BADF : E_NOSYS;
    const uint8_t *p = buf;
    for (unsigned i = 0; i < len; i++) putchar_both(p[i]);
    return (int)len;
}

int bios_lseek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; return E_NOSYS; }
int bios_opendir(const char *path) { (void)path; return E_NOSYS; }
int bios_readdir(int handle, struct dirent *ent) { (void)handle; (void)ent; return E_NOSYS; }
int bios_closedir(int handle) { (void)handle; return E_NOSYS; }
int bios_sd_read_block(uint32_t lba, void *buf) { (void)lba; (void)buf; return E_NOSYS; }
int bios_sd_write_block(uint32_t lba, const void *buf) { (void)lba; (void)buf; return E_NOSYS; }
int bios_exec(const char *path, const char *args) { (void)path; (void)args; return E_NOSYS; }

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
    if (!info) return E_INVAL;
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
    if (!info) return E_INVAL;
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
    /* whole frames by the VBLANK counter, remainder by a calibrated loop */
    uint32_t frame_ms = 1000 / ticks_per_sec;
    uint32_t frames = ms / frame_ms;
    if (frames) {
        uint32_t t0 = ticks;
        while ((uint32_t)(ticks - t0) < frames) ;
    }
    delay_us((ms - frames * frame_ms) * 1000);
}

int bios_set_audio_cb(void *cb)
{
    uint16_t sr = irq_disable();
    audio_cb = (irq_hook_fn)cb;
    irq_update_inten();
    irq_restore(sr);
    return 0;
}
