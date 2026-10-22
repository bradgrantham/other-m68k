/* Single-producer / single-consumer byte ring. The producer (an ISR) only writes head, the
 * consumer only writes tail; 16-bit stores are atomic on the 68000 so no masking is needed. */
#include "bios.h"

void queue_init(struct queue *q, uint8_t *buf, uint16_t size)
{
    q->buf = buf;
    q->size = size;
    q->head = q->tail = 0;
}

int queue_put(struct queue *q, uint8_t c)
{
    uint16_t next = (uint16_t)((q->head + 1) & (q->size - 1));
    if (next == q->tail)
        return -1;
    q->buf[q->head] = c;
    q->head = next;
    return 0;
}

int queue_get(struct queue *q)
{
    if (q->tail == q->head)
        return -1;
    uint8_t c = q->buf[q->tail];
    q->tail = (uint16_t)((q->tail + 1) & (q->size - 1));
    return c;
}

int queue_empty(const struct queue *q)
{
    return q->tail == q->head;
}

int queue_count(const struct queue *q)
{
    return (q->head - q->tail) & (q->size - 1);
}

/* Consumer side: is byte c anywhere in the queue? (A scan for ESC that keeps type-ahead.) */
int queue_contains(const struct queue *q, uint8_t c)
{
    uint16_t t = q->tail, h = q->head;
    while (t != h) {
        if (q->buf[t] == c) return 1;
        t = (uint16_t)((t + 1) & (q->size - 1));
    }
    return 0;
}
