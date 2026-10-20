/* BIOS C entry: called by crt0.S after POST, vector copy and .data/.bss setup. */
#include "bios.h"

uint16_t post_rom_sum;

void putchar_both(int c)
{
    con_putc(c);
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

void print_banner(void)
{
    uint16_t stored = *(volatile uint16_t *)(ROM_END - 2);
    uint16_t id = IO_ID;
    kprintf("%s BIOS %s\n", BOARD_NAME, FW_VERSION);
    kprintf("ROM %uK sum %04x (%s)  RAM %uK\n", (unsigned)(ROM_SIZE / 1024), stored,
            post_rom_sum == 0 ? "ok" : "BAD", (unsigned)(RAM_SIZE / 1024));
    kprintf("ID %04x  %s  serial TX %s\n", id, video_standard == VIDEO_STD_PAL ? "PAL" : "NTSC",
            serial_hw_tx ? "hw" : "sw");
    kprintf("user RAM %06x-%06x  fb0 %06x\n", (unsigned)USER_BASE, (unsigned)(USER_END - 1),
            (unsigned)FB_PAGE0_ADDR);
}

void bios_main(void)
{
    /* LED1 only: C init (post.S left it that way; gpio_shadow was stored by crt0) */
    IO_INTEN = 0;
    serial_init();
    input_init();
    if (IO_INTSTAT & INTSTAT_PALJMP) {
        SW_PAL_SET();
        video_standard = VIDEO_STD_PAL;
        ticks_per_sec = 50;
    } else {
        SW_NTSC_SET();
    }
    SW_PAGE0_SET();
    video_page = 0;
    con_init((uint8_t *)FB_PAGE0_ADDR);
    irq_init();
    serial_puts("\n");
    print_banner();
    gpio_clear(GPIO_LED0 | GPIO_LED1);
    /* crt0 continues into monitor_restart() */
}
