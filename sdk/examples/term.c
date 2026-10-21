/* term: a terminal on serial port A, the port the BIOS releases after the POST.
 *
 * Opens /dev/ttya, greets whoever is on the other end, then copies bytes both ways:
 * what arrives on the port goes to the video console, what is typed on the keyboard goes
 * out the port. ESC on the keyboard ends it. This is the smallest "OS personality"
 * pattern: the BIOS console stays on the screen, the program owns the serial line.
 *
 *   term            copy both ways until ESC
 *   term -echo      also show on the video console what the keyboard sends */
#include <stdio.h>
#include <string.h>
#include "syscalls.h"

int main(int argc, char **argv)
{
    int echo = argc > 1 && !strcmp(argv[1], "-echo");
    int tty = sys_open(O_RDWR, "/dev/ttya");      /* fopen("/dev/ttya", "r+") works too */
    if (tty < 0) { printf("term: cannot open /dev/ttya\n"); return 1; }
    printf("term: serial A at 38400 8N1, ESC quits\n");
    const char hello[] = "\r\nother-m68k term: type here, it shows on the screen\r\n";
    sys_write(tty, sizeof hello - 1, hello);

    unsigned long in = 0, out = 0;
    for (;;) {
        int k = sys_pollchar();                 /* keyboard only: serial is not console input */
        if (k == 0x1B) break;
        if (k >= 0) {
            char c = (char)k;
            sys_write(tty, 1, &c);
            out++;
            if (echo) putchar(k == '\r' ? '\n' : k);
        }
        int c;
        while ((c = sys_serial_getc()) >= 0) {  /* non-blocking peek at the same ring read() drains */
            in++;
            if (c == '\r') continue;            /* terminals send CR LF; the console wants \n */
            putchar(c);
        }
        fflush(stdout);
    }
    const char bye[] = "\r\nterm: bye\r\n";
    sys_write(tty, sizeof bye - 1, bye);
    sys_close(tty);
    printf("\nterm: %lu bytes in, %lu out\n", in, out);
    return 0;
}
