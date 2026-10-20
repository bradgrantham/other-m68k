/* cat: print files through newlib stdio (fopen -> open -> sys_open, fread -> sys_read).
 * `run cat.elf README.TXT [more files]`. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "syscalls.h"

int main(int argc, char **argv)
{
    char buf[512];
    int status = 0;

    if (argc < 2) { printf("usage: cat file...\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { printf("cat: %s: %s\n", argv[i], strerror(errno)); status = 1; continue; }
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            for (size_t k = 0; k < n; k++)
                if (buf[k] != '\r') putchar(buf[k]);
            fflush(stdout);
        }
        if (ferror(f)) { printf("cat: %s: read error\n", argv[i]); status = 1; }
        fclose(f);
    }
    return status;
}
