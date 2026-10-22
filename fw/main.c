/* BIOS C entry: called by crt0.S after POST, vector copy and .data/.bss setup. */
#include "bios.h"
#include "sd.h"
#include "fs.h"
#include "clock.h"

uint16_t post_rom_sum;
int console_serial = 1;             /* mirror the console to DUART A: only until the banner is out */

void putchar_both(int c)
{
    con_putc(c);
    if (!console_serial) return;
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

void print_banner(void)
{
    uint16_t stored = *(volatile uint16_t *)(ROM_BASE + ROM_IMAGE_SIZE - 2);
    uint16_t id = IO_ID;
    kprintf("%s BIOS %s\n", BOARD_NAME, FW_VERSION);
    kprintf("ROM %uK sum %04x (%s) C: %uK RAM %uK\n", (unsigned)(ROM_IMAGE_SIZE / 1024), stored,
            post_rom_sum == 0 ? "ok" : "BAD", (unsigned)(FLASH_VOL_SIZE / 1024), (unsigned)(RAM_SIZE / 1024));
    kprintf("ID %04x  %s  DUART %u 8N1\n", id, video_standard == VIDEO_STD_PAL ? "PAL" : "NTSC",
            (unsigned)SERIAL_BAUD);
    kprintf("user RAM %06x-%06x  fb0 %06x\n", (unsigned)USER_BASE, (unsigned)(USER_END - 1),
            (unsigned)FB_PAGE0_ADDR);
}

void bios_main(void)
{
    /* LED1 only: C init (post.S left it that way; gpio_shadow was stored by crt0) */
    IO_INTEN = 0;
    inten_shadow = 0;
    serial_init();
    input_init();
    sd_hw_init();                       /* all six card lines released */
    fs_init();                          /* C: (flash) and D: (card), mounted on first use */
    if (IO_INTSTAT & INTSTAT_PALJMP) {
        SW_PAL_SET();
        video_standard = VIDEO_STD_PAL;
        ticks_per_sec = 50;
        clock_frame_clocks = VIDEO_PAL_LINE_CLOCKS * VIDEO_PAL_LINES;
    } else {
        SW_NTSC_SET();
    }
    SW_PAGE0_SET();
    video_page = 0;
    con_init((uint8_t *)FB_PAGE0_ADDR);
    audio_init();                           /* idle pattern into the PDM shifter, no stream */
    irq_init();
    serial_puts("\n");
    print_banner();
    gpio_clear(GPIO_LED0 | GPIO_LED1);
    /* The POST and the banner went to video and serial. From here on serial port A is
     * released (spec serial.console): the monitor, exceptions and programs print to video
     * only and read the keyboard only; a program claims the port through /dev/ttya or the
     * serial_putc/serial_getc syscalls. The RX interrupt keeps filling serial_rx_q. */
    console_serial = 0;
    /* crt0 continues into monitor_restart() */
}
