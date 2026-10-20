/* Serial port (68681 DUART channel A) and the write-only GPIO register's shadow.
 *
 * The DUART itself is programmed by duart_reset_init (duart.S) on the reset path, before
 * the POST, so that the POST can print through it. Here: the receive interrupt enable,
 * the polled transmit path and the RX ring consumer. Receive happens in irq_duart
 * (irq.c), which fills serial_rx_q. */
#include "bios.h"

uint16_t gpio_shadow;

void gpio_write(uint16_t v)
{
    uint16_t sr = irq_disable();
    gpio_shadow = v;
    IO_GPIO = v;
    irq_restore(sr);
}

void gpio_set(uint16_t bits)
{
    uint16_t sr = irq_disable();
    gpio_shadow |= bits;
    IO_GPIO = gpio_shadow;
    irq_restore(sr);
}

void gpio_clear(uint16_t bits)
{
    uint16_t sr = irq_disable();
    gpio_shadow &= (uint16_t)~bits;
    IO_GPIO = gpio_shadow;
    irq_restore(sr);
}

void led_set(int n, int on)
{
    uint16_t bit = n ? GPIO_LED1 : GPIO_LED0;
    if (on) gpio_set(bit); else gpio_clear(bit);
}

void delay_us(uint32_t us)
{
    delay_loops((us * 2 + 2) / 3);      /* 18 clocks per loop at 12 MHz */
}

void serial_init(void)
{
    /* Channel A is already running (duart_reset_init). Drop anything that arrived during
     * the POST and clear stale error flags, then let RXRDYA raise the DUART IRQ pin; it
     * reaches the CPU at level 3 once irq_init sets INTEN.DUART. */
    while (DUART_SRA & SRA_RXRDY) (void)DUART_RBA;
    DUART_CRA = DUART_CR_RESET_ERR;
    DUART_IMR = IMR_RXRDYA;
}

void serial_putc(int c)
{
    while (!(DUART_SRA & SRA_TXRDY)) ;      /* transmit holding register busy */
    DUART_TBA = (uint8_t)c;
}

void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

int serial_getc(void)
{
    return queue_get(&serial_rx_q);
}
