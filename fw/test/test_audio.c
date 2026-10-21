/* Host tests of audio.c: the PDM encoder table (same algorithm as tools/pdmtab.py), the
 * sample queue, its ISR service and the INTEN hand-shake. */
#include "test.h"
#include "audio.h"

/* BIOS globals audio.c refers to */
uint16_t inten_shadow;
irq_hook_fn audio_cb;
irq_hook_fn irq_user_hook[8];
void irq_update_inten(void) { }

static uint8_t out[8192];
static unsigned out_n, inten_updates;
void host_audio_out(uint8_t p) { if (out_n < sizeof out) out[out_n] = p; out_n++; }
void host_inten_update(void) { inten_updates++; inten_shadow |= INTEN_HSYNC; }

extern uint16_t pdm_table_host[32768];

static uint16_t ref_entry(unsigned acc, unsigned v7)
{
    unsigned pattern = 0;
    for (int i = 0; i < 8; i++) {
        unsigned s = acc + 2 * v7;
        pattern = (pattern << 1) | (s >> 8);
        acc = s & 255;
    }
    return (uint16_t)((acc << 8) | pattern);
}

static unsigned ones(unsigned v) { unsigned n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

void test_audio(void)
{
    for (unsigned acc = 0; acc < 256; acc++)
        for (unsigned v7 = 0; v7 < 128; v7++)
            pdm_table_host[(acc << 7) | v7] = ref_entry(acc, v7);

    /* table properties: the pattern density follows the sample, the accumulator wraps */
    CHECK_EQ(pdm_table_host[(0 << 7) | 64] & 0xFF, 0x55);         /* silence level 128 from acc 0 -> 01010101, acc back to 0 */
    CHECK_EQ(pdm_table_host[(0 << 7) | 0], 0x0000);               /* 0 -> all zeros, acc stays 0 */
    CHECK_EQ(ones(pdm_table_host[(0 << 7) | 127] & 0xFF), 7);     /* 254/256 density: 7 of 8 ones (first line) */
    CHECK_EQ(ones(pdm_table_host[(0 << 7) | 32] & 0xFF), 2);      /* 64/256 -> 2 ones */
    for (unsigned v7 = 0; v7 < 128; v7++) {                       /* density over 32 lines = v7/128 */
        unsigned acc = 0, n = 0;
        for (int l = 0; l < 32; l++) {
            uint16_t e = pdm_table_host[(acc << 7) | v7];
            n += ones(e & 0xFF); acc = e >> 8;
        }
        CHECK(n == 2 * v7 || n == 2 * v7 + 1);                    /* 256 bits * v7/128 = 2*v7 (+ rounding) */
    }

    /* queue mechanics */
    audio_init();
    CHECK_EQ(out_n, 1); CHECK_EQ(out[0], AUDIO_IDLE_PATTERN);
    CHECK_EQ(audio_free(), AUD_RING_SIZE);
    static uint8_t s[5000];
    memset(s, 128, sizeof s);
    inten_shadow = 0; inten_updates = 0;
    CHECK_EQ(audio_push(s, 10), 10);
    CHECK_EQ(audio_free(), AUD_RING_SIZE - 10);
    CHECK_EQ(inten_updates, 1);                                   /* became non-empty: enable */
    CHECK_EQ(audio_push(s, 10), 10);
    CHECK_EQ(inten_updates, 1);                                   /* already enabled: no second write */
    CHECK_EQ(audio_push(s, 5000), AUD_RING_SIZE - 20);            /* clipped to the free space */
    CHECK_EQ(audio_free(), 0);
    CHECK_EQ(audio_push(s, 1), 0);
    out_n = 0;
    for (int i = 0; i < AUD_RING_SIZE; i++) audio_isr_queue();
    CHECK_EQ(out_n, AUD_RING_SIZE);
    int all_idle = 1;
    for (int i = 0; i < AUD_RING_SIZE; i++) if (out[i] != 0x55) all_idle = 0;
    CHECK(all_idle);                                              /* 128 encodes to exactly 0x55 every line (50 %) */
    CHECK_EQ(audio_free(), AUD_RING_SIZE);
    audio_isr_queue();                                            /* empty: idle pattern */
    CHECK_EQ(out[AUD_RING_SIZE], AUDIO_IDLE_PATTERN);

    /* a ramp: the accumulator carries across samples and across pushes */
    for (int i = 0; i < 64; i++) s[i] = (uint8_t)(i * 4);
    audio_init(); out_n = 0;
    CHECK_EQ(audio_push(s, 32), 32);
    CHECK_EQ(audio_push(s + 32, 32), 32);
    for (int i = 0; i < 64; i++) audio_isr_queue();
    unsigned total = 0;
    for (int i = 0; i < 64; i++) total += ones(out[i]);
    /* expected density sum: sum over i of (i*4>>1)/128*8 bits = sum i*4/256*8 = sum i/8 = 252 */
    CHECK(total >= 251 && total <= 253);
    /* wrap-around of the free running indices */
    audio_init();
    for (int round = 0; round < 40; round++) {
        CHECK_EQ(audio_push(s, 2000), 2000);
        for (int i = 0; i < 2000; i++) audio_isr_queue();
    }
    CHECK_EQ(audio_free(), AUD_RING_SIZE);
    audio_flush();
    CHECK_EQ(audio_free(), AUD_RING_SIZE);
    /* mode flag */
    audio_update_mode(); CHECK_EQ(audio_state.slow, 0);
    irq_user_hook[IRQ_HSYNC_LEVEL] = (irq_hook_fn)1; audio_update_mode(); CHECK_EQ(audio_state.slow, 1);
    irq_user_hook[IRQ_HSYNC_LEVEL] = 0; audio_cb = (irq_hook_fn)1; audio_update_mode(); CHECK_EQ(audio_state.slow, 1);
    audio_cb = 0; audio_update_mode(); CHECK_EQ(audio_state.slow, 0);
}
