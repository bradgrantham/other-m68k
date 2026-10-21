/* audio.c: 8-bit sample queue for the PDM shifter (spec rev 5).
 *
 * audio_write (TRAP #15) hands in unsigned 8-bit samples at the video line rate. They are
 * encoded here, at queue time, into one PDM pattern byte each with the ROM table
 * (first-order sigma-delta, 8-bit accumulator carried from sample to sample, 7 bits of
 * the sample used), so that the HSYNC interrupt (irq_hsync.S) only has to pop a byte and
 * write it to IO_AUDIO: 254 clocks per line on the fast path. The ISR writes the idle
 * pattern and switches INTEN.HSYNC off when the queue runs dry; audio_push switches it
 * back on. Single producer (syscall, user context) / single consumer (ISR): the producer
 * writes the bytes first and publishes head afterwards; the consumer only advances tail.
 * Both indices are free running 16-bit values masked on use, so head == tail is empty
 * and head - tail == AUD_RING_SIZE is full.
 *
 * Under HOST_TEST the register write and the INTEN update are routed to the test. */
#include "bios.h"
#include "audio.h"

struct audio_state audio_state;

_Static_assert(__builtin_offsetof(struct audio_state, ring) == AUD_RING, "AUD_RING");
_Static_assert(__builtin_offsetof(struct audio_state, head) == AUD_HEAD, "AUD_HEAD");
_Static_assert(__builtin_offsetof(struct audio_state, tail) == AUD_TAIL, "AUD_TAIL");
_Static_assert(__builtin_offsetof(struct audio_state, accw) == AUD_ACCW, "AUD_ACCW");
_Static_assert(__builtin_offsetof(struct audio_state, slow) == AUD_SLOW, "AUD_SLOW");
_Static_assert((AUD_RING_SIZE & AUD_RING_MASK) == 0, "ring size must be a power of two");

#ifdef HOST_TEST
void host_audio_out(uint8_t pattern);
void host_inten_update(void);
uint16_t pdm_table_host[32768];
#define AUDIO_OUT(p)   host_audio_out((uint8_t)(p))
#define INTEN_UPDATE() host_inten_update()
#define IRQ_OFF()      0
#define IRQ_ON(sr)     ((void)(sr))
#define PDM_TABLE      pdm_table_host
#else
#define AUDIO_OUT(p)   (IO_AUDIO = (uint16_t)(p))
#define INTEN_UPDATE() irq_update_inten()
#define IRQ_OFF()      irq_disable()
#define IRQ_ON(sr)     irq_restore(sr)
#define PDM_TABLE      pdm_table
#endif

void audio_init(void)
{
    audio_state.head = audio_state.tail = 0;
    audio_state.accw = 0;
    audio_state.slow = 0;
    AUDIO_OUT(AUDIO_IDLE_PATTERN);
}

unsigned audio_free(void)
{
    return AUD_RING_SIZE - (uint16_t)(audio_state.head - audio_state.tail);
}

unsigned audio_push(const uint8_t *samples, unsigned n)
{
    unsigned room = audio_free();
    if (n > room) n = room;
    uint16_t h = audio_state.head, accw = audio_state.accw;
    for (unsigned i = 0; i < n; i++) {
        uint16_t e = PDM_TABLE[(uint16_t)(accw | samples[i]) >> 1];   /* (acc<<7) | (s>>1) */
        audio_state.ring[h++ & AUD_RING_MASK] = (uint8_t)e;
        accw = e & 0xFF00;
    }
    audio_state.accw = accw;
    audio_state.head = h;                       /* publish after the bytes are in place */
    if (n) {
        /* The ISR may have just disabled itself on an empty queue: re-enable while masked
         * so it cannot run between the test and the INTEN write. */
        uint16_t sr = IRQ_OFF();
        if (!(inten_shadow & INTEN_HSYNC) && audio_state.head != audio_state.tail)
            INTEN_UPDATE();
        IRQ_ON(sr);
    }
    return n;
}

void audio_flush(void)
{
    uint16_t sr = IRQ_OFF();
    audio_state.tail = audio_state.head;
    audio_state.accw = 0;
    IRQ_ON(sr);
    AUDIO_OUT(AUDIO_IDLE_PATTERN);
}

void audio_update_mode(void)
{
    audio_state.slow = (audio_cb || irq_user_hook[IRQ_HSYNC_LEVEL]) ? 1 : 0;
}

/* Slow path (a level-4 user hook is installed, no audio callback): same as the asm fast
 * path but INTEN stays on, since the hook wants the interrupt anyway. */
void audio_isr_queue(void)
{
    uint16_t t = audio_state.tail;
    if (t != audio_state.head) {
        AUDIO_OUT(audio_state.ring[t & AUD_RING_MASK]);
        audio_state.tail = (uint16_t)(t + 1);
    } else {
        AUDIO_OUT(AUDIO_IDLE_PATTERN);
    }
}
