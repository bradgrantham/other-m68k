/* abench: how much CPU the HSYNC audio paths leave for the program (spec rev 6).
 * Counts iterations of a fixed loop over a few VBLANK ticks for: no audio; an empty
 * callback; a C callback that pops a pre-made pattern from a ring and writes IO_AUDIO;
 * the same in asm; and the BIOS stream (irq_hsync.S fast path) playing three queued
 * 512-byte buffers. The stream case runs over 5 ticks = 1310 lines, inside the 1536
 * queued bytes, so nothing else touches the BIOS during the window. Also times
 * pdm_init and pdm_encode from the SDK. */
#include <stdio.h>
#include <stdint.h>
#include "syscalls.h"
#include "hw.h"
#include "pdm.h"

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

static uint32_t measure(int ticks)
{
    uint32_t t0 = sys_ticks(), n = 0;
    while (sys_ticks() == t0) ;
    t0 = sys_ticks();
    while (sys_ticks() - t0 < (uint32_t)ticks) {
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
    uint32_t base = measure(TICKS);
    printf("no audio      : %lu iterations / %d ticks (100.0%%)\n", (unsigned long)base, TICKS);
    struct { const char *name; void (*fn)(void); } cases[] = {
        { "empty callback", cb_empty }, { "C callback    ", cb_c }, { "asm callback  ", cb_asm } };
    for (unsigned k = 0; k < 3; k++) {
        samples = 0;
        sys_set_audio_cb(cases[k].fn);
        uint32_t r = measure(TICKS);
        sys_set_audio_cb(0);
        report(cases[k].name, base, r);
    }
    /* BIOS stream: queue all three buffers of silence patterns, measure while the fast
     * handler plays them (5 ticks fit inside the 1536 queued lines), then let it finish. */
    uint32_t base5 = measure(5);
    uint8_t *b = sys_audio_start(AUDIO_BUF_MAX);
    for (int k = 0; k < AUDIO_NBUF && b; k++) {
        for (int i = 0; i < AUDIO_BUF_MAX; i++) b[i] = pdm_silence();
        b = sys_audio_next(b, 0);                /* the last hand-in returns NULL: none free */
    }
    uint32_t r = measure(5);
    report("BIOS stream   ", base5, r);
    sys_audio_last(0, 1);
    /* the SDK encoder: table build time, then clocks per sample of pdm_encode */
    uint32_t t0 = sys_ticks();
    while (sys_ticks() == t0) ;
    t0 = sys_ticks();
    pdm_init();
    uint32_t tb = sys_ticks() - t0;
    printf("pdm_init      : %lu ticks (%lu ms)\n", (unsigned long)tb, (unsigned long)tb * 1000 / 60);
    static uint8_t pat[4096];
    t0 = sys_ticks();
    while (sys_ticks() == t0) ;
    t0 = sys_ticks();
    uint32_t n = 0;
    while (sys_ticks() - t0 < TICKS) { pdm_encode(pat, buf, 4096); n += 4096; }
    /* 12 ticks = 12 * 764 * 262 clocks, minus nothing else running */
    uint32_t per = (uint32_t)((uint64_t)TICKS * 764 * 262 / n);
    printf("pdm_encode    : %lu samples in %d ticks, about %lu clocks per sample\n", (unsigned long)n, TICKS, (unsigned long)per);
    return 0;
}
