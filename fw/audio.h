/* audio.h: PDM sample queue shared by audio.c (C) and irq_hsync.S (asm). Spec rev 5. */
#ifndef AUDIO_H
#define AUDIO_H

#define AUD_RING_SIZE   4096            /* bytes; must be a power of two */
#define AUD_RING_MASK   (AUD_RING_SIZE - 1)
/* struct audio_state layout (checked by static asserts in audio.c) */
#define AUD_RING        0               /* uint8_t ring[AUD_RING_SIZE]: PDM patterns, already encoded */
#define AUD_HEAD        4096            /* uint16_t: producer index, free running (mask on use) */
#define AUD_TAIL        4098            /* uint16_t: consumer index (the ISR), free running */
#define AUD_ACCW        4100            /* uint16_t: sigma-delta accumulator << 8 */
#define AUD_SLOW        4102            /* uint16_t: nonzero when audio_cb or a level-4 hook is installed */
#define AUDIO_IDLE_PATTERN 0xAA         /* 50 % density: the silence level (sample 128) */

#ifndef __ASSEMBLER__
#include <stdint.h>
struct audio_state {
    uint8_t  ring[AUD_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    uint16_t accw;
    uint16_t slow;
};
extern struct audio_state audio_state;
extern const uint16_t pdm_table[32768];   /* pdmtab.S (ROM) */

void     audio_init(void);                /* idle pattern to the shifter, empty queue */
unsigned audio_free(void);                /* bytes that audio_push would accept now */
unsigned audio_push(const uint8_t *samples, unsigned n);  /* encode + queue, start the ISR; returns accepted */
void     audio_flush(void);               /* drop queued samples, idle pattern, accumulator reset */
void     audio_update_mode(void);         /* recompute audio_state.slow from audio_cb / level-4 hook */
void     audio_isr_queue(void);           /* the ISR's queue service in C (used on the slow path) */
#endif
#endif
