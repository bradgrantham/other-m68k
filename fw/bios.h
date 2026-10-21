/* other-m68k BIOS: shared declarations. Hardware constants come from generated/hw.h. */
#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "hw.h"
#include "duart.h"
#include "syscalls.h"
#include "bios_syscalls.h"
#include "audio.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#ifdef HOST_TEST
#include <string.h>
#else
/* string.c / memops.S */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strcpy(char *dst, const char *src);
#endif

/* ---- printf.c (freestanding, no floating point) ---- */
typedef void (*fmt_out_fn)(void *ctx, char c);
int kvformat(fmt_out_fn out, void *ctx, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int kprintf(const char *fmt, ...);      /* to the console (video + serial) */

/* ---- queue.c: single-producer single-consumer byte ring ---- */
struct queue {
    uint8_t *buf;
    uint16_t size;          /* power of two */
    volatile uint16_t head; /* producer writes */
    volatile uint16_t tail; /* consumer writes */
};
void queue_init(struct queue *q, uint8_t *buf, uint16_t size);
int queue_put(struct queue *q, uint8_t c);  /* 0 ok, -1 full */
int queue_get(struct queue *q);             /* byte or -1 */
int queue_empty(const struct queue *q);
int queue_count(const struct queue *q);

/* ---- srec.c: Motorola S-record line parser ---- */
struct srec_state {
    void (*store)(void *ctx, uint32_t addr, const uint8_t *data, int n);
    void *ctx;
    uint32_t records;       /* data records accepted */
    uint32_t bytes;
    uint32_t errors;        /* lines rejected (bad checksum/length/char) */
    uint32_t min_addr, max_addr; /* inclusive range of stored data */
    uint32_t entry;         /* from S7/S8/S9 */
    uint8_t have_entry;
    uint8_t have_data;
    uint8_t done;           /* S7/S8/S9 seen */
};
void srec_init(struct srec_state *s,
               void (*store)(void *, uint32_t, const uint8_t *, int), void *ctx);
/* 0 = data/header record ok, 1 = termination record (s->done), -1 = bad line */
int srec_line(struct srec_state *s, const char *line);

/* ---- hexdump.c ---- */
#define HEXDUMP_BYTES_PER_LINE 8
/* Formats "aaaaaa xx xx .. cccccccc" for n <= 8 bytes into out (>= 40 chars). */
void hexdump_line(char *out, uint32_t addr, const uint8_t *data, int n);

/* ---- ps2.c: set-2 scan code decoder (pure) ---- */
void ps2_init(void);
void ps2_feed(uint8_t b);
/* Sinks, provided by input.c (or the host test). */
void input_kbd_push(uint8_t ascii);
void input_key_event_push(uint16_t ev);

/* ---- console.c: 40x30 text on a 320x240 RGB332 page ---- */
#define CON_COLS (VIDEO_WIDTH / 8)
#define CON_ROWS (VIDEO_HEIGHT / 8)
void con_init(uint8_t *fb);
void con_clear(void);
void con_putc(int c);
void con_puts(const char *s);
void con_set_cursor(int x, int y);
void con_get_cursor(int *x, int *y);
void con_set_colors(uint8_t fg, uint8_t bg);
void con_cursor_show(int on);   /* draw/erase the block cursor */
extern uint8_t *con_fb;

/* ---- serial.c / duart.S ---- */
extern uint16_t gpio_shadow;            /* image of the write-only IO_GPIO; every write goes through here */
void serial_init(void);                 /* DUART was programmed by _start; this enables the RX interrupt */
void serial_putc(int c);                /* raw byte, no CR/LF translation; polls SRA.TXRDY */
void serial_puts(const char *s);        /* '\n' -> "\r\n" */
int serial_getc(void);                  /* -1 if none */
void gpio_write(uint16_t v);
void gpio_set(uint16_t bits);
void gpio_clear(uint16_t bits);
void led_set(int n, int on);
void delay_loops(uint32_t n);           /* duart.S: 18 clocks each */
void delay_us(uint32_t us);

/* ---- input.c ---- */
void input_init(void);
int input_pollchar(void);               /* keyboard first, then serial; -1 if none */
int input_getchar(void);                /* blocking */
int input_key_event(void);              /* -1 if none */
extern struct queue serial_rx_q;
extern uint32_t serial_rx_overruns;
extern uint32_t ps2_errors;

/* ---- irq.c ---- */
typedef void (*irq_hook_fn)(void);
extern volatile uint32_t ticks;
extern volatile uint32_t ticks_per_sec;
extern irq_hook_fn irq_user_hook[8];    /* indexed by level 1..4 */
extern irq_hook_fn audio_cb;
extern uint16_t inten_shadow;         /* last value written to the write-only INTEN */
void irq_init(void);
void irq_reset_user(void);              /* drop user hooks, default INTEN */
void irq_update_inten(void);
uint16_t irq_disable(void);             /* crt0.S */
void irq_restore(uint16_t sr);
void irq_vblank(void);
void irq_ps2(void);
void irq_duart(void);
void irq_hsync(void);                   /* irq_hsync.S */

/* ---- exception.c ---- */
struct exc_frame {
    uint32_t d[8];          /* 0  */
    uint32_t a[7];          /* 32 */
    uint32_t ssp;           /* 60 */
    uint32_t usp;           /* 64 */
    uint32_t pc;            /* 68 */
    uint32_t fault_addr;    /* 72 */
    uint16_t sr;            /* 76 */
    uint16_t vector;        /* 78 */
    uint16_t fc;            /* 80 */
    uint16_t ir;            /* 82 */
    uint16_t group0;        /* 84 */
    uint16_t valid;         /* 86: set once an exception has been recorded */
};
extern struct exc_frame exc;
void exception_entry(void);
void exception_print_regs(void);
const char *exception_name(int vector);

/* ---- syscall.c ---- */
struct trap_frame { uint32_t d0, d1, d2, d3, a0, a1; };
void syscall_dispatch(struct trap_frame *f);
void user_cleanup(void);                /* restore BIOS state after a user program */
extern uint32_t video_standard;         /* VIDEO_STD_NTSC / VIDEO_STD_PAL */
extern uint32_t video_page;             /* displayed page */

/* ---- monitor.c ---- */
void monitor_main(void) __attribute__((noreturn));
void monitor_restart(void) __attribute__((noreturn));    /* crt0.S */
void run_user(uint32_t pc, uint32_t usp) __attribute__((noreturn)); /* crt0.S; see exec.c user_launch */
void cpu_reset(void) __attribute__((noreturn));          /* crt0.S */
void cpu_reboot(void) __attribute__((noreturn));         /* crt0.S: jump through the reset vectors in the flash */
extern uint32_t last_entry;             /* default address for `go` */

/* ---- sd.c / fs.c / elf.c / exec.c: see sd.h, fs.h, elf.h, exec.h ---- */

/* ---- main.c ---- */
extern uint16_t post_rom_sum;           /* 16-bit word sum from POST (0 = good) */
void bios_main(void);
void print_banner(void);
void putchar_both(int c);               /* console + serial */

#endif
