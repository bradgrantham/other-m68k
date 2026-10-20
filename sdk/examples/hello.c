/* hello: newlib printf over the BIOS console, plus the information system calls. */
#include <stdio.h>
#include "syscalls.h"

int main(void)
{
    struct video_info vi;
    struct mem_info mi;
    struct con_info ci;

    printf("Hello from newlib on other-m68k!\n");
    if (sys_video_info(&vi) == 0)
        printf("video %lux%lu %lubpp stride %lu\n page0 %06lx page1 %06lx cur %lu %s\n",
               (unsigned long)vi.width, (unsigned long)vi.height, (unsigned long)vi.bpp,
               (unsigned long)vi.bytes_per_line, (unsigned long)vi.page_addr[0],
               (unsigned long)vi.page_addr[1], (unsigned long)vi.current_page,
               vi.standard == VIDEO_STD_PAL ? "PAL" : "NTSC");
    if (sys_mem_info(&mi) == 0)
        printf("user RAM %06lx-%06lx\n (to %06lx without page 1)\n",
               (unsigned long)mi.user_start, (unsigned long)mi.user_end - 1,
               (unsigned long)mi.user_end_without_fb1 - 1);
    if (sys_con_info(&ci) == 0)
        printf("console %lux%lu cursor %lu,%lu\n", (unsigned long)ci.cols, (unsigned long)ci.rows,
               (unsigned long)ci.cursor_x, (unsigned long)ci.cursor_y);
    printf("ticks %lu\n", (unsigned long)sys_ticks());
    printf("press a key: ");
    fflush(stdout);
    int c = sys_getchar();
    printf("got 0x%02x\n", c);
    return 0;
}
