/* fstest: write a file with stdio, read it back with the raw system calls, report, and
 * optionally chain to another program: `run fstest.elf` or `run fstest.elf exec hello.elf`.
 * Exercises O_CREAT/O_TRUNC translation in the newlib shim, lseek, sys_exec and argv. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "syscalls.h"

int main(int argc, char **argv)
{
    static char buf[1024];
    const char *name = "TEST.TXT";
    int fd, n, bad = 0;

    FILE *f = fopen(name, "w");
    if (!f) { printf("fstest: create %s: %s\n", name, strerror(errno)); return 1; }
    const int linelen = (int)strlen("line 00 of the fstest file\n");           /* 27 */
    for (int i = 0; i < 40; i++) fprintf(f, "line %02d of the fstest file\n", i);   /* 1080 bytes, >2 sectors */
    if (fclose(f) != 0) { printf("fstest: close: %s\n", strerror(errno)); return 1; }

    fd = sys_open(O_RDONLY, name);
    if (fd < 0) { printf("fstest: open: %d\n", fd); return 1; }
    int size = sys_lseek(fd, 0, SEEK_END);
    sys_lseek(fd, linelen * 5, SEEK_SET);                  /* start of line 5 */
    n = sys_read(fd, (unsigned)linelen, buf);
    buf[n > 0 ? n : 0] = 0;
    if (size != 40 * linelen) { printf("size %d != %d\n", size, 40 * linelen); bad++; }
    if (strcmp(buf, "line 05 of the fstest file\n")) { printf("line 5 = \"%s\"\n", buf); bad++; }
    sys_close(fd);

    f = fopen(name, "a");                                   /* O_APPEND */
    if (!f) { printf("fstest: append: %s\n", strerror(errno)); return 1; }
    fputs("appended\n", f);
    fclose(f);
    f = fopen(name, "r");
    int lines = 0;
    while (fgets(buf, sizeof buf, f)) lines++;
    fclose(f);
    if (lines != 41) { printf("lines %d != 41\n", lines); bad++; }

    printf("fstest: %s (%d bytes, %d lines)\n", bad ? "FAIL" : "OK", size, lines);
    if (argc >= 3 && !strcmp(argv[1], "exec")) {
        printf("exec %s\n", argv[2]);
        fflush(stdout);
        int r = sys_exec(argv[2], "from fstest");
        printf("exec failed: %d\n", r);                     /* only on failure */
        return 1;
    }
    return bad ? 1 : 0;
}
