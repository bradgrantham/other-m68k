/* abench: how much CPU the HSYNC audio paths leave for the program (spec rev 5).
 * Counts iterations of a fixed loop over 12 VBLANK ticks (0.2 s) for: no callback; an
 * empty callback; a C callback that pops a pre-made pattern from a ring and writes
 * IO_AUDIO; the same in asm; and the BIOS queue player (irq_hsync.S fast path) draining
 * a full queue. 12 ticks = 3141 lines, so a 4096-sample queue does not run dry. */
#include <stdio.h>
#include <stdint.h>
#include "syscalls.h"
#include "hw.h"

#define TICKS 12
#define LINES_PER_TICK 262                   /* NTSC */

static uint8_t buf[4096];
static volatile uint32_t samples;
uint16_t ab_idx;

static void cb_empty(void) { }

static void cb_c(void)
{
    IO_AUDIO = buf[ab_idx++ & 4095];
    samples++;
}

/* d0/d1/a0/a1 may be used freely: the BIOS saves what the call clobbers. */
__asm__(
"    .text\n"
"    .globl cb_asm\n"
"cb_asm:\n"
"    move.w  ab_idx,%d0\n"
"    addq.w  #1,ab_idx\n"
"    and.w   #4095,%d0\n"
"    lea     buf,%a0\n"
"    move.b  (%a0,%d0.w),%d0\n"
"    move.w  %d0,0xF0000A\n"
"    rts\n");
extern void cb_asm(void);

static uint32_t measure(void)
{
    uint32_t t0 = sys_ticks(), n = 0;
    while (sys_ticks() == t0) ;
    t0 = sys_ticks();
    while (sys_ticks() - t0 < TICKS) {
        for (int i = 0; i < 64; i++) buf[(n + i) & 4095] += (uint8_t)i;   /* touch memory, a little arithmetic */
        n++;
    }
    return n;
}

static void report(const char *name, uint32_t base, uint32_t r)
{
    uint32_t lost = base - r;
    uint32_t permille = (uint32_t)((uint64_t)lost * 1000 / base);
    uint32_t clocks = (uint32_t)((uint64_t)lost * 764 * 10 / base);   /* 764 clocks per NTSC line */
    printf("%s: %lu iterations (%lu.%lu%% left), %lu.%lu clocks per line\n", name, (unsigned long)r,
           (unsigned long)((1000 - permille) / 10), (unsigned long)((1000 - permille) % 10),
           (unsigned long)(clocks / 10), (unsigned long)(clocks % 10));
}

int main(void)
{
    for (int i = 0; i < 4096; i++) buf[i] = (uint8_t)(i * 37);
    sys_set_audio_cb(0);
    uint32_t base = measure();
    printf("no audio      : %lu iterations / %d ticks (100.0%%)\n", (unsigned long)base, TICKS);
    struct { const char *name; void (*fn)(void); } cases[] = {
        { "empty callback", cb_empty }, { "C callback    ", cb_c }, { "asm callback  ", cb_asm } };
    for (unsigned k = 0; k < 3; k++) {
        samples = 0;
        sys_set_audio_cb(cases[k].fn);
        uint32_t r = measure();
        sys_set_audio_cb(0);
        report(cases[k].name, base, r);
    }
    /* BIOS queue player: fill the queue (a ramp, so the encoder does real work) and measure
     * while the ISR drains it. The sys_audio_write itself is outside the window. */
    for (int i = 0; i < 4096; i++) buf[i] = (uint8_t)(i >> 4);
    unsigned off = 0;
    while (off < 4096) { int r = sys_audio_write(buf + off, 4096 - off); if (r <= 0) break; off += (unsigned)r; }
    uint32_t r = measure();
    report("queue player  ", base, r);
    while (sys_audio_write(buf, 0) < 4096) sys_wait_vblank();
    /* and the producer side: what one chunk of 512 samples costs to encode and queue */
    uint32_t t0 = sys_ticks();
    while (sys_ticks() == t0) ;
    t0 = sys_ticks();
    uint32_t pushed = 0;
    while (sys_ticks() - t0 < TICKS) {
        int n = sys_audio_write(buf, 512);
        if (n > 0) pushed += (uint32_t)n;
    }
    printf("sys_audio_write: %lu samples queued in %d ticks while playing\n", (unsigned long)pushed, TICKS);
    while (sys_audio_write(buf, 0) < 4096) sys_wait_vblank();
    return 0;
}
