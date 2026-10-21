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
#define AUDIO_BUF_MAX 512
#define AUDIO_NBUF 3

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
    SYS_audio_start = 30,
    SYS_audio_next = 31,
    SYS_audio_last = 32,
    SYS_COUNT = 33
};

/* ---- user-side prototypes (implemented by generated/syscall_stubs.S) ---- */
void sys_exit(int code); /* Terminate the program and return to the monitor. */
int sys_putchar(int c); /* Write a character to the video console. */
int sys_getchar(void); /* Blocking read of one character from the keyboard. */
int sys_pollchar(void); /* Non-blocking keyboard read; -1 if no character is available. */
int sys_puts(const char * s); /* Write a NUL-terminated string to the video console (no newline added). */
int sys_serial_putc(int c); /* Write a byte to serial port A (free for programs after the POST; see also /dev/ttya). */
int sys_serial_getc(void); /* Non-blocking read from serial port A's receive ring; -1 if none. */
uint32_t sys_ticks(void); /* Frames since boot (VBLANK counter, ~60 Hz). */
void sys_wait_vblank(void); /* Block until the next vertical blanking interval. */
int sys_video_info(struct video_info * info); /* Fill in framebuffer geometry, page addresses and current page. */
int sys_video_set_page(int page); /* Select displayed framebuffer page (0 or 1); takes effect at next frame. */
int sys_open(int flags, const char * path); /* Open a file on the SD card (8.3 names, O_* flags from the ABI; O_CREAT/O_TRUNC/O_APPEND values differ from newlib's, the SDK shim translates). Returns fd >= 3 or a negative error; fds 0..2 are the console. Paths starting with /dev/ open a device instead (serial.devices); /dev/ttya needs no card. */
int sys_close(int fd); /* Close a file. */
int sys_read(int fd, unsigned len, void * buf); /* Read up to len bytes. Returns count or negative error. fd 0 is the keyboard (blocks for the first byte, then returns what is queued); /dev/ttya behaves the same on the serial ring. */
int sys_write(int fd, unsigned len, const void * buf); /* Write len bytes. Returns count or negative error. fd 1 and 2 are the video console; a /dev/ttya descriptor sends raw bytes to serial port A. */
int sys_lseek(int fd, int offset, int whence); /* Reposition; returns new offset or negative error. */
int sys_opendir(const char * path); /* Open a directory; returns handle or negative error. */
int sys_readdir(int handle, struct dirent * ent); /* Next entry; 1 if filled, 0 at end, negative error. */
int sys_closedir(int handle); /* Close a directory handle. */
int sys_sd_read_block(uint32_t lba, void * buf); /* Raw 512-byte block read. */
int sys_sd_write_block(uint32_t lba, const void * buf); /* Raw 512-byte block write. */
void * sys_set_irq_handler(int level, void * handler); /* Install a user interrupt handler for level 1..4 (called from the BIOS ISR in supervisor mode at that interrupt's own priority, so higher levels can still interrupt it); returns the previous one. NULL restores default. */
int sys_mem_info(struct mem_info * info); /* Report the user RAM region available to the program. */
int sys_con_set_cursor(int x, int y); /* Move the console text cursor. */
int sys_con_clear(void); /* Clear the console. */
int sys_con_info(struct con_info * info); /* Console columns, rows, cursor position. */
int sys_exec(const char * path, const char * args); /* Load and run another ELF executable from the SD card with argument string `args`. Returns a negative error only if the file cannot be opened or does not fit user RAM, before any user memory is overwritten; otherwise the new program replaces the caller and its exit returns to the monitor. */
int sys_key_event(void); /* Next raw keyboard event: bit 8 = release, bits 7..0 = PS/2 set-2 scan code (E0-prefixed codes have bit 9 set); -1 if none. */
void sys_sleep_ms(uint32_t ms); /* Delay for approximately ms milliseconds. */
int sys_set_audio_cb(void * cb); /* Install a function called from the HSYNC interrupt once per video line (15.7 kHz NTSC) in supervisor mode; it takes over the AUDIO register (the buffer stream of audio_start/next/last is not serviced while a callback is installed, and audio_start fails while one is) and should write one PDM pattern per call. NULL disables. */
uint8_t * sys_audio_start(unsigned bufsize); /* Begin streaming PDM pattern bytes to the AUDIO register, one byte per video line (15.7 kHz NTSC, 15.6 kHz PAL). The BIOS owns AUDIO_NBUF buffers of AUDIO_BUF_MAX bytes; bufsize (1..AUDIO_BUF_MAX) is how many bytes of each the program will fill and the hardware will play. Resets the stream (anything queued is dropped, the output goes to the idle pattern) and returns the first buffer to fill. Non-blocking. Returns NULL if bufsize is out of range or an audio callback is installed. The bytes are raw hardware patterns: encode samples with the SDK's pdm_encode (first-order sigma-delta) or write patterns directly. Playback starts at the first audio_next. */
uint8_t * sys_audio_next(const uint8_t * filled, int block); /* Hand the BIOS a buffer the program has filled (one obtained from audio_start or a previous audio_next) and get the next buffer to fill. Buffers play in the order handed in; the HSYNC interrupt is enabled while any are queued. If no buffer is free: block != 0 waits (interrupts enabled) until the one being played finishes, which paces the program to the audio rate; block == 0 returns NULL at once and the program retries later (the handed buffer is still accepted). filled == NULL hands nothing in and only tries to obtain a free buffer. Returns NULL also when the stream is not started, or when filled is not one of the BIOS buffers. */
int sys_audio_last(const uint8_t * filled, int block); /* Hand in the final buffer (filled may be NULL for none) and end the stream. block != 0 waits until everything queued has played; block == 0 returns at once and the queued buffers finish playing on their own. When the last byte has played the handler writes the idle pattern 0xAA and disables the HSYNC interrupt, so idle audio costs no CPU. Returns 0, or E_INVAL if the stream was not started. Exiting the program stops the stream at once instead: queued buffers are dropped, the idle pattern is written and the interrupt is disabled. */

#endif /* SYSCALLS_H */
