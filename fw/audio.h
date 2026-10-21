/* audio.h: PDM pattern streaming by buffer hand-off, shared by audio.c (C) and irq_hsync.S
 * (asm). Spec rev 6: audio_start / audio_next / audio_last. */
#ifndef AUDIO_H
#define AUDIO_H

/* struct audio_state layout (checked by static asserts in audio.c). The buffers come first
 * so that buffer i starts at audio_state + i * AUDIO_BUF_MAX. */
#define AUD_BUF         0               /* uint8_t buf[AUDIO_NBUF][AUDIO_BUF_MAX]: pattern bytes */
#define AUD_PLAY        1536            /* uint8_t *: next byte to write, 0 when nothing is playing */
#define AUD_END         1540            /* uint8_t *: end of the playing buffer */
#define AUD_BUFSIZE     1544            /* uint32_t: bytes played per buffer (1..AUDIO_BUF_MAX) */
#define AUD_HEAD        1548            /* uint8_t: FIFO slot of the playing buffer (consumer, free running) */
#define AUD_TAIL        1549            /* uint8_t: FIFO slot to append to (producer, free running) */
#define AUD_ENDING      1550            /* uint8_t: audio_last was called: finish and go idle */
#define AUD_STARTED     1551            /* uint8_t: stream open (cleared by the handler when it ends) */
#define AUD_FIFO        1552            /* uint8_t fifo[4]: buffer indices in play order */
#define AUD_OWNED       1556            /* uint8_t: buffer handed to the program, or AUD_NONE */
#define AUD_FIFO_MASK   3
#define AUD_NONE        0xFF
#define AUDIO_IDLE_PATTERN 0xAA         /* 50 % density: the silence level (sample 128) */

#ifndef __ASSEMBLER__
#include <stdint.h>
#include "syscalls.h"                   /* AUDIO_NBUF, AUDIO_BUF_MAX */
struct audio_state {
    uint8_t  buf[AUDIO_NBUF][AUDIO_BUF_MAX];
    uint8_t *volatile play;
    uint8_t *end;
    uint32_t bufsize;
    volatile uint8_t head;
    volatile uint8_t tail;
    uint8_t  ending;
    volatile uint8_t started;
    uint8_t  fifo[4];
    uint8_t  owned;
    uint8_t  pad[3];
};
extern struct audio_state audio_state;

void     audio_init(void);                /* idle pattern to the shifter, no stream */
uint8_t *audio_start(unsigned bufsize);   /* bios_audio_start */
uint8_t *audio_next(const uint8_t *filled, int block);
int      audio_last(const uint8_t *filled, int block);
void     audio_stop(void);                /* drop everything, idle pattern (sys_exit, start) */
int      audio_active(void);              /* a buffer is playing or queued: INTEN.HSYNC wanted */
void     audio_update_mode(void);         /* pick the fast or the slow level-4 handler */
void     audio_isr_service(void);         /* one line of the player in C (the slow handler) */
#endif
#endif
