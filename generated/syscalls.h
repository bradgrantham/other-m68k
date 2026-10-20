/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
#ifndef SYSCALLS_H
#define SYSCALLS_H
#include <stdint.h>

#define SYSCALL_TRAP 15

/* ---- ABI constants ---- */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 256
#define O_TRUNC 512
#define O_APPEND 1024
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define ATTR_DIR 1
#define ATTR_READONLY 2
#define ATTR_HIDDEN 4
#define VIDEO_STD_NTSC 0
#define VIDEO_STD_PAL 1
#define E_NOENT -2
#define E_ACCES -13
#define E_FAULT -14
#define E_IO -5
#define E_NOEXEC -8
#define E_BADF -9
#define E_NOMEM -12
#define E_EXIST -17
#define E_NOTDIR -20
#define E_NODEV -19
#define E_ISDIR -21
#define E_INVAL -22
#define E_NFILE -23
#define E_NOSPC -28
#define E_ROFS -30
#define E_NAMETOOLONG -36
#define E_NOSYS -38
#define KEY_RELEASE 256
#define KEY_EXTENDED 512

/* ---- ABI structures ---- */
struct video_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t bytes_per_line;
    uint32_t page_addr[2];
    uint32_t current_page;
    uint32_t standard;
};
struct mem_info {
    uint32_t user_start;
    uint32_t user_end;
    uint32_t user_end_without_fb1;
};
struct con_info {
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_x;
    uint32_t cursor_y;
};
struct dirent {
    char name[64];
    uint32_t size;
    uint32_t attr;
};

/* ---- syscall numbers ---- */
enum {
    SYS_exit = 0,
    SYS_putchar = 1,
    SYS_getchar = 2,
    SYS_pollchar = 3,
    SYS_puts = 4,
    SYS_serial_putc = 5,
    SYS_serial_getc = 6,
    SYS_ticks = 7,
    SYS_wait_vblank = 8,
    SYS_video_info = 9,
    SYS_video_set_page = 10,
    SYS_open = 11,
    SYS_close = 12,
    SYS_read = 13,
    SYS_write = 14,
    SYS_lseek = 15,
    SYS_opendir = 16,
    SYS_readdir = 17,
    SYS_closedir = 18,
    SYS_sd_read_block = 19,
    SYS_sd_write_block = 20,
    SYS_set_irq_handler = 21,
    SYS_mem_info = 22,
    SYS_con_set_cursor = 23,
    SYS_con_clear = 24,
    SYS_con_info = 25,
    SYS_exec = 26,
    SYS_key_event = 27,
    SYS_sleep_ms = 28,
    SYS_set_audio_cb = 29,
    SYS_COUNT = 30
};

/* ---- user-side prototypes (implemented by generated/syscall_stubs.S) ---- */
void sys_exit(int code); /* Terminate the program and return to the monitor. */
int sys_putchar(int c); /* Write a character to the console (video + serial). */
int sys_getchar(void); /* Blocking read of one character from keyboard or serial. */
int sys_pollchar(void); /* Non-blocking read; -1 if no character is available. */
int sys_puts(const char * s); /* Write a NUL-terminated string to the console (no newline added). */
int sys_serial_putc(int c); /* Write a byte to the serial port only. */
int sys_serial_getc(void); /* Non-blocking serial read; -1 if none. */
uint32_t sys_ticks(void); /* Frames since boot (VBLANK counter, ~60 Hz). */
void sys_wait_vblank(void); /* Block until the next vertical blanking interval. */
int sys_video_info(struct video_info * info); /* Fill in framebuffer geometry, page addresses and current page. */
int sys_video_set_page(int page); /* Select displayed framebuffer page (0 or 1); takes effect at next frame. */
int sys_open(int flags, const char * path); /* Open a file on the SD card (8.3 names, O_* flags from the ABI; O_CREAT/O_TRUNC/O_APPEND values differ from newlib's, the SDK shim translates). Returns fd >= 3 or a negative error; fds 0..2 are the console. */
int sys_close(int fd); /* Close a file. */
int sys_read(int fd, unsigned len, void * buf); /* Read up to len bytes. Returns count or negative error. */
int sys_write(int fd, unsigned len, const void * buf); /* Write len bytes. Returns count or negative error. */
int sys_lseek(int fd, int offset, int whence); /* Reposition; returns new offset or negative error. */
int sys_opendir(const char * path); /* Open a directory; returns handle or negative error. */
int sys_readdir(int handle, struct dirent * ent); /* Next entry; 1 if filled, 0 at end, negative error. */
int sys_closedir(int handle); /* Close a directory handle. */
int sys_sd_read_block(uint32_t lba, void * buf); /* Raw 512-byte block read. */
int sys_sd_write_block(uint32_t lba, const void * buf); /* Raw 512-byte block write. */
void * sys_set_irq_handler(int level, void * handler); /* Install a user interrupt handler (called from the BIOS ISR with interrupts masked); returns the previous one. NULL restores default. */
int sys_mem_info(struct mem_info * info); /* Report the user RAM region available to the program. */
int sys_con_set_cursor(int x, int y); /* Move the console text cursor. */
int sys_con_clear(void); /* Clear the console. */
int sys_con_info(struct con_info * info); /* Console columns, rows, cursor position. */
int sys_exec(const char * path, const char * args); /* Load and run another ELF executable from the SD card with argument string `args`. Returns a negative error only if the file cannot be opened or does not fit user RAM, before any user memory is overwritten; otherwise the new program replaces the caller and its exit returns to the monitor. */
int sys_key_event(void); /* Next raw keyboard event: bit 8 = release, bits 7..0 = PS/2 set-2 scan code (E0-prefixed codes have bit 9 set); -1 if none. */
void sys_sleep_ms(uint32_t ms); /* Delay for approximately ms milliseconds. */
int sys_set_audio_cb(void * cb); /* Install a function called at 15.7 kHz from the HSYNC interrupt to drive GPIO.AUDIO; NULL disables. */

#endif /* SYSCALLS_H */
