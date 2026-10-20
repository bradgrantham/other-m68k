/* Serial port glue: GPIO shadow, LEDs, TX path selection (hardware transmitter if the
 * CPLD has one, else the bit-bang loop in sertx.S), RX from the ring filled by the ISR. */
#include "bios.h"

uint16_t gpio_shadow;
uint8_t serial_hw_tx;

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
    /* Probe for a hardware transmitter: a write to SERDATA raises SERTXBUSY if one exists.
     * Software-only glue ignores the write. A CR is harmless on the terminal. */
    IO_SERDATA = '\r';
    serial_hw_tx = (IO_INTSTAT & INTSTAT_SERTXBUSY) ? 1 : 0;
    if (serial_hw_tx)
        while (IO_INTSTAT & INTSTAT_SERTXBUSY) ;
}

void serial_putc(int c)
{
    if (serial_hw_tx) {
        while (IO_INTSTAT & INTSTAT_SERTXBUSY) ;
        IO_SERDATA = (uint16_t)(c & 0xFF);
    } else {
        serial_tx_byte(c);
    }
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
