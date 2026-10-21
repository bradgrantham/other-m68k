/* Host tests of audio.c: the buffer hand-off stream (audio_start / next / last), its FIFO,
 * the C line service (the twin of the asm fast path) and the INTEN / handler-mode
 * hand-shakes. The handler is driven by hand, one line per audio_isr_service(); a
 * blocking wait inside audio.c runs lines through host_audio_wait(). */
#include "test.h"
#include "audio.h"

/* BIOS globals audio.c refers to */
uint16_t inten_shadow;
irq_hook_fn audio_cb;
irq_hook_fn irq_user_hook[8];
void irq_update_inten(void) { }

static uint8_t out[16384];
static unsigned out_n, inten_updates, waits, vector_slow;
void host_audio_out(uint8_t p) { if (out_n < sizeof out) out[out_n] = p; out_n++; }
void host_inten_update(void)
{
    inten_updates++;
    if (audio_cb || irq_user_hook[IRQ_HSYNC_LEVEL] || audio_active()) inten_shadow |= INTEN_HSYNC;
    else inten_shadow &= (uint16_t)~INTEN_HSYNC;
}
void host_vector_set(int slow) { vector_slow = (unsigned)slow; }
void host_audio_wait(void) { waits++; audio_isr_service(); }

static int is_buf(const uint8_t *p)
{
    for (int i = 0; i < AUDIO_NBUF; i++) if (p == audio_state.buf[i]) return 1;
    return 0;
}

static void fill(uint8_t *b, unsigned n, unsigned tag)
{
    for (unsigned i = 0; i < n; i++) b[i] = (uint8_t)(tag * 64 + (i & 63));
}

void test_audio(void)
{
    audio_init();
    CHECK_EQ(out_n, 1); CHECK_EQ(out[0], AUDIO_IDLE_PATTERN);
    CHECK(!audio_active());

    /* start: argument checks, a callback blocks it */
    CHECK(audio_start(0) == 0);
    CHECK(audio_start(AUDIO_BUF_MAX + 1) == 0);
    audio_cb = (irq_hook_fn)1;
    CHECK(audio_start(256) == 0);
    audio_cb = 0;
    CHECK_EQ(audio_last(0, 0), E_INVAL);                     /* not started */
    CHECK(audio_next(0, 0) == 0);

    out_n = 0; inten_shadow = 0; inten_updates = 0;
    uint8_t *b = audio_start(256);
    CHECK(b == audio_state.buf[0]);
    CHECK_EQ(out[0], AUDIO_IDLE_PATTERN);                    /* start resets the output */
    CHECK_EQ(audio_state.started, 1);
    CHECK(!(inten_shadow & INTEN_HSYNC));

    /* handing in a buffer the program does not own is refused */
    CHECK(audio_next(audio_state.buf[1], 0) == 0);
    CHECK(audio_next(out, 0) == 0);

    /* first hand-off starts playback at once and returns a different buffer */
    fill(b, 256, 1);
    uint8_t *b2 = audio_next(b, 0);
    CHECK(b2 != 0 && b2 != b && is_buf(b2));
    CHECK(audio_state.play == b);
    CHECK(audio_state.end == b + 256);
    CHECK(inten_shadow & INTEN_HSYNC);

    /* the handler streams the bytes in order; an underrun holds the last pattern */
    out_n = 0;
    for (int i = 0; i < 256; i++) audio_isr_service();
    CHECK_EQ(out_n, 256);
    int ok = 1;
    for (int i = 0; i < 256; i++) if (out[i] != b[i]) ok = 0;
    CHECK(ok);
    CHECK(audio_state.play == 0);
    CHECK(!(inten_shadow & INTEN_HSYNC));
    CHECK_EQ(audio_state.started, 1);                        /* still open: not ending */
    audio_isr_service();                                     /* a stray line does nothing */
    CHECK_EQ(out_n, 256);

    /* queue all three: the third request without a free buffer returns NULL non-blocking */
    fill(b2, 256, 2);
    uint8_t *b3 = audio_next(b2, 0);
    CHECK(b3 != 0 && is_buf(b3) && b3 != b2);
    CHECK(audio_state.play == b2);                           /* restarted */
    CHECK(inten_shadow & INTEN_HSYNC);
    fill(b3, 256, 3);
    uint8_t *b4 = audio_next(b3, 0);
    CHECK(b4 != 0 && is_buf(b4) && b4 != b2 && b4 != b3);     /* the one freed earlier */
    fill(b4, 256, 4);
    CHECK(audio_next(b4, 0) == 0);                           /* all three queued: none free */
    CHECK(audio_next(0, 0) == 0);                            /* still none, nothing handed */
    CHECK_EQ((uint8_t)(audio_state.tail - audio_state.head), 3);

    /* blocking: the wait runs lines until the playing buffer is released */
    waits = 0; out_n = 0;
    uint8_t *b5 = audio_next(0, 1);
    CHECK(b5 == b2);                                         /* the buffer that just finished */
    CHECK_EQ(waits, 256);
    CHECK_EQ(out_n, 256);
    CHECK(audio_state.play == b3);

    /* the rest plays in hand-in order */
    for (int i = 0; i < 512; i++) audio_isr_service();
    CHECK_EQ(out_n, 768);
    ok = 1;
    for (int i = 0; i < 256; i++) if (out[i] != b2[i] || out[256 + i] != b3[i] || out[512 + i] != b4[i]) ok = 0;
    CHECK(ok);
    CHECK(audio_state.play == 0);

    /* last: the final buffer, then the idle pattern and the stream closes */
    fill(b5, 256, 5);
    out_n = 0; waits = 0;
    CHECK_EQ(audio_last(b5, 1), 0);
    CHECK_EQ(waits, 256);
    CHECK_EQ(out_n, 257);
    CHECK_EQ(out[255], b5[255]);
    CHECK_EQ(out[256], AUDIO_IDLE_PATTERN);
    CHECK_EQ(audio_state.started, 0);
    CHECK(!(inten_shadow & INTEN_HSYNC));
    CHECK(audio_next(0, 1) == 0);                            /* closed */

    /* last with nothing queued closes immediately */
    b = audio_start(16);
    out_n = 0;
    CHECK_EQ(audio_last(0, 1), 0);
    CHECK_EQ(out_n, 1); CHECK_EQ(out[0], AUDIO_IDLE_PATTERN);
    CHECK_EQ(audio_state.started, 0);

    /* a smaller bufsize plays only that many bytes of each buffer */
    b = audio_start(16);
    fill(b, 16, 6);
    b2 = audio_next(b, 0);
    out_n = 0;
    for (int i = 0; i < 16; i++) audio_isr_service();
    CHECK_EQ(out_n, 16);
    CHECK(audio_state.play == 0);
    CHECK(b2 != 0);

    /* stop (sys_exit): everything dropped, idle, interrupt off */
    fill(b2, 16, 7);
    audio_next(b2, 0);
    out_n = 0;
    audio_stop();
    CHECK_EQ(out[0], AUDIO_IDLE_PATTERN);
    CHECK_EQ(audio_state.started, 0);
    CHECK(audio_state.play == 0);
    CHECK(!(inten_shadow & INTEN_HSYNC));

    /* handler selection: a callback or a level-4 hook selects the slow handler */
    audio_update_mode(); CHECK_EQ(vector_slow, 0);
    audio_cb = (irq_hook_fn)1; audio_update_mode(); CHECK_EQ(vector_slow, 1); audio_cb = 0;
    irq_user_hook[IRQ_HSYNC_LEVEL] = (irq_hook_fn)1; audio_update_mode(); CHECK_EQ(vector_slow, 1);
    irq_user_hook[IRQ_HSYNC_LEVEL] = 0; audio_update_mode(); CHECK_EQ(vector_slow, 0);

    /* sys_read destinations inside the BIOS buffers are allowed only while a stream is open */
    uint8_t *b0 = &audio_state.buf[0][0];
    CHECK(!audio_buf_range_ok(b0, 1));                                   /* no stream */
    uint8_t *got = audio_start(512);
    CHECK(got == b0);
    CHECK(audio_buf_range_ok(b0, AUDIO_NBUF * AUDIO_BUF_MAX));
    CHECK(audio_buf_range_ok(b0 + AUDIO_NBUF * AUDIO_BUF_MAX - 1, 1));
    CHECK(!audio_buf_range_ok(b0 + AUDIO_NBUF * AUDIO_BUF_MAX - 1, 2));  /* runs off the end */
    CHECK(!audio_buf_range_ok(b0 + AUDIO_NBUF * AUDIO_BUF_MAX, 1));
    CHECK(!audio_buf_range_ok(b0 - 1, 1));
    CHECK(!audio_buf_range_ok(b0, (uint32_t)-1));
    audio_stop();
    CHECK(!audio_buf_range_ok(b0, 1));
}
