/* Input queues: serial RX (filled by the level-3 ISR), keyboard ASCII and raw key events
 * (filled by the PS/2 decoder from the level-2 ISR). All SPSC rings. */
#include "bios.h"

static uint8_t serial_buf[512];
static uint8_t kbd_buf[256];
static uint8_t key_ev_buf[128];     /* 64 events, two bytes each */
struct queue serial_rx_q;
static struct queue kbd_q, key_ev_q;
uint32_t serial_rx_overruns;
uint32_t ps2_errors;

void input_init(void)
{
    queue_init(&serial_rx_q, serial_buf, sizeof serial_buf);
    queue_init(&kbd_q, kbd_buf, sizeof kbd_buf);
    queue_init(&key_ev_q, key_ev_buf, sizeof key_ev_buf);
    ps2_init();
}

void input_kbd_push(uint8_t c)
{
    queue_put(&kbd_q, c);
}

void input_key_event_push(uint16_t ev)
{
    if (queue_count(&key_ev_q) + 2 >= (int)key_ev_q.size) return;   /* keep pairs intact */
    queue_put(&key_ev_q, (uint8_t)(ev >> 8));
    queue_put(&key_ev_q, (uint8_t)ev);
}

int input_key_event(void)
{
    if (queue_count(&key_ev_q) < 2) return -1;
    int hi = queue_get(&key_ev_q);
    int lo = queue_get(&key_ev_q);
    return (hi << 8) | lo;
}

int input_pollchar(void)
{
    int c = queue_get(&kbd_q);
    if (c < 0) c = queue_get(&serial_rx_q);
    return c;
}

int input_getchar(void)
{
    int c;
    while ((c = input_pollchar()) < 0) ;
    return c;
}
