/* audio.c: PDM pattern streaming by buffer hand-off (spec rev 6).
 *
 * The BIOS owns AUDIO_NBUF buffers of AUDIO_BUF_MAX bytes. The program gets one from
 * audio_start, fills it with hardware pattern bytes (the SDK's pdm_encode turns 8-bit
 * samples into them), hands it in with audio_next and gets the next free one back, blocking
 * if asked until the level-4 handler has finished a buffer: that paces the program to the
 * line rate. audio_last hands in the final buffer and, if asked, waits for the end.
 *
 * Producer (syscall, user context) and consumer (irq_hsync.S) share a 4-slot FIFO of buffer
 * indices: the producer appends at tail, the consumer plays fifo[head] and advances head
 * when the buffer is done. Only the producer writes tail/fifo, only the consumer writes head;
 * play/end are written by the consumer, and by the producer only while nothing is playing,
 * with interrupts masked. The BIOS does no per-sample work outside the handler.
 *
 * Two level-4 handlers exist (irq_hsync.S): the fast one (no callback, no level-4 hook)
 * and the slow one, which calls audio_isr_service() below and the hooks; audio_update_mode
 * points autovector 28 at the right one, so the fast path has no mode test.
 *
 * Under HOST_TEST the register write, the INTEN update, the vector switch and the blocking
 * wait are routed to the test. */
#include "bios.h"
#include "audio.h"

struct audio_state audio_state;

#ifndef HOST_TEST                                   /* offsets for irq_hsync.S: 32-bit pointers */
_Static_assert(__builtin_offsetof(struct audio_state, buf) == AUD_BUF, "AUD_BUF");
_Static_assert(__builtin_offsetof(struct audio_state, play) == AUD_PLAY, "AUD_PLAY");
_Static_assert(__builtin_offsetof(struct audio_state, end) == AUD_END, "AUD_END");
_Static_assert(__builtin_offsetof(struct audio_state, bufsize) == AUD_BUFSIZE, "AUD_BUFSIZE");
_Static_assert(__builtin_offsetof(struct audio_state, head) == AUD_HEAD, "AUD_HEAD");
_Static_assert(__builtin_offsetof(struct audio_state, tail) == AUD_TAIL, "AUD_TAIL");
_Static_assert(__builtin_offsetof(struct audio_state, ending) == AUD_ENDING, "AUD_ENDING");
_Static_assert(__builtin_offsetof(struct audio_state, started) == AUD_STARTED, "AUD_STARTED");
_Static_assert(__builtin_offsetof(struct audio_state, fifo) == AUD_FIFO, "AUD_FIFO");
_Static_assert(__builtin_offsetof(struct audio_state, owned) == AUD_OWNED, "AUD_OWNED");
_Static_assert(AUDIO_NBUF * AUDIO_BUF_MAX == AUD_PLAY, "buffers must fill the front of the struct");
_Static_assert(AUDIO_NBUF <= AUD_FIFO_MASK, "the FIFO needs one spare slot");
#endif

#ifdef HOST_TEST
void host_audio_out(uint8_t pattern);
void host_inten_update(void);
void host_vector_set(int slow);
void host_audio_wait(void);
#define AUDIO_OUT(p)   host_audio_out((uint8_t)(p))
#define INTEN_UPDATE() host_inten_update()
#define VECTOR_SET(s)  host_vector_set(s)
#define VECTOR_IS_BIOS() 1
#define WAIT()         host_audio_wait()
#define IRQ_OFF()      0
#define IRQ_ON(sr)     ((void)(sr))
#else
extern void irq_hsync_fast(void), irq_hsync_slow(void);
#define AUDIO_OUT(p)   (IO_AUDIO = (uint16_t)(p))
#define INTEN_UPDATE() irq_update_inten()
#define VECTOR_SET(s)  (*(volatile uint32_t *)IRQ_HSYNC_VECTOR_ADDR = (uint32_t)((s) ? irq_hsync_slow : irq_hsync_fast))
#define VECTOR_IS_BIOS() (*(volatile uint32_t *)IRQ_HSYNC_VECTOR_ADDR == (uint32_t)irq_hsync_fast || \
                          *(volatile uint32_t *)IRQ_HSYNC_VECTOR_ADDR == (uint32_t)irq_hsync_slow)
#define WAIT()         ((void)0)
#define IRQ_OFF()      irq_disable()
#define IRQ_ON(sr)     irq_restore(sr)
#endif

static void reset_state(void)
{
    audio_state.play = 0;
    audio_state.end = 0;
    audio_state.head = audio_state.tail = 0;
    audio_state.ending = 0;
    audio_state.started = 0;
    audio_state.owned = AUD_NONE;
}

void audio_init(void)
{
    reset_state();
    audio_state.bufsize = AUDIO_BUF_MAX;
    AUDIO_OUT(AUDIO_IDLE_PATTERN);
}

void audio_stop(void)
{
    uint16_t sr = IRQ_OFF();
    reset_state();
    IRQ_ON(sr);
    AUDIO_OUT(AUDIO_IDLE_PATTERN);
    INTEN_UPDATE();
}

int audio_active(void)
{
    return audio_state.play != 0;
}

/* Called from irq_update_inten with interrupts masked. The slow handler is needed while a
 * callback or a level-4 hook is installed. A vector 28 that points anywhere else belongs to
 * the program (it replaced the level-4 vector itself) and is left alone; audio_reset_vector
 * (program exit) puts the BIOS handler back unconditionally. */
void audio_update_mode(void)
{
    if (VECTOR_IS_BIOS()) VECTOR_SET(audio_cb || irq_user_hook[IRQ_HSYNC_LEVEL]);
}

void audio_reset_vector(void)
{
    VECTOR_SET(audio_cb || irq_user_hook[IRQ_HSYNC_LEVEL]);
}

uint8_t *audio_start(unsigned bufsize)
{
    if (bufsize == 0 || bufsize > AUDIO_BUF_MAX || audio_cb) return 0;
    audio_stop();
    audio_state.bufsize = bufsize;
    audio_state.started = 1;
    audio_state.owned = 0;
    return audio_state.buf[0];
}

/* Index of a BIOS buffer, or -1. */
static int buf_index(const uint8_t *p)
{
    for (int i = 0; i < AUDIO_NBUF; i++)
        if (p == audio_state.buf[i]) return i;
    return -1;
}

static unsigned queued(void)
{
    return (uint8_t)(audio_state.tail - audio_state.head);
}

/* Append the program's buffer to the FIFO; start it at once if nothing is playing. */
static void enqueue(int idx)
{
    audio_state.fifo[audio_state.tail & AUD_FIFO_MASK] = (uint8_t)idx;
    audio_state.tail++;                            /* publish */
    audio_state.owned = AUD_NONE;
    uint16_t sr = IRQ_OFF();
    if (!audio_state.play) {
        audio_state.play = audio_state.buf[idx];
        audio_state.end = audio_state.buf[idx] + audio_state.bufsize;
        INTEN_UPDATE();                            /* play != 0 now: INTEN.HSYNC goes on */
    }
    IRQ_ON(sr);
}

/* A buffer that is neither queued nor owned by the program, or -1. A buffer leaves the
 * FIFO window when the handler advances head past it. */
static int free_buffer(void)
{
    if (audio_state.owned != AUD_NONE) return -1;
    if (queued() >= AUDIO_NBUF) return -1;
    for (int i = 0; i < AUDIO_NBUF; i++) {
        int busy = 0;
        for (uint8_t s = audio_state.head; s != audio_state.tail; s++)
            if (audio_state.fifo[s & AUD_FIFO_MASK] == i) busy = 1;
        if (!busy) return i;
    }
    return -1;
}

uint8_t *audio_next(const uint8_t *filled, int block)
{
    if (!audio_state.started || audio_state.ending) return 0;
    if (filled) {
        int idx = buf_index(filled);
        if (idx < 0 || idx != audio_state.owned) return 0;
        enqueue(idx);
    }
    int idx;
    while ((idx = free_buffer()) < 0) {
        if (!block || !audio_state.started) return 0;
        WAIT();                                    /* interrupts are enabled: the handler frees one */
    }
    audio_state.owned = (uint8_t)idx;
    return audio_state.buf[idx];
}

int audio_last(const uint8_t *filled, int block)
{
    if (!audio_state.started) return E_INVAL;
    if (filled) {
        int idx = buf_index(filled);
        if (idx >= 0 && idx == audio_state.owned) enqueue(idx);
    }
    audio_state.owned = AUD_NONE;
    uint16_t sr = IRQ_OFF();
    audio_state.ending = 1;
    if (!audio_state.play) {                       /* nothing queued at all: finish here */
        audio_state.started = 0;
        AUDIO_OUT(AUDIO_IDLE_PATTERN);
    }
    IRQ_ON(sr);
    if (block)
        while (audio_state.started) WAIT();
    return 0;
}

/* One line of the player, in C: the slow handler calls this when no callback is installed
 * (the asm fast path does the same thing). At the end of a buffer: release it, take the
 * next, or go idle (idle pattern only when the stream is ending; an underrun just holds
 * the last pattern). INTEN.HSYNC is recomputed through irq_update_inten, which keeps it on
 * for a level-4 hook. */
void audio_isr_service(void)
{
    uint8_t *p = audio_state.play;
    if (!p) return;
    AUDIO_OUT(*p++);
    if (p != audio_state.end) { audio_state.play = p; return; }
    uint8_t h = (uint8_t)(audio_state.head + 1);
    audio_state.head = h;
    if (h != audio_state.tail) {
        int idx = audio_state.fifo[h & AUD_FIFO_MASK];
        audio_state.play = audio_state.buf[idx];
        audio_state.end = audio_state.buf[idx] + audio_state.bufsize;
        return;
    }
    audio_state.play = 0;
    if (audio_state.ending) {
        AUDIO_OUT(AUDIO_IDLE_PATTERN);
        audio_state.started = 0;
    }
    INTEN_UPDATE();
}
