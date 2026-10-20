/* ls: list a directory with the BIOS directory calls, and show how argv arrives.
 * `run ls.elf` lists the root; `run ls.elf /sub` lists /sub. */
#include <stdio.h>
#include <string.h>
#include "syscalls.h"

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/";
    struct dirent e;
    int h, rc, files = 0, dirs = 0;
    unsigned long bytes = 0;

    printf("argc %d", argc);
    for (int i = 0; i < argc; i++) printf(" [%s]", argv[i]);
    printf("\n");

    h = sys_opendir(path);
    if (h < 0) { printf("ls: %s: error %d\n", path, h); return 1; }
    while ((rc = sys_readdir(h, &e)) > 0) {
        if (e.attr & ATTR_DIR) { printf("%-12s    <DIR>\n", e.name); dirs++; }
        else { printf("%-12s %8lu%s\n", e.name, (unsigned long)e.size, (e.attr & ATTR_READONLY) ? " R" : ""); files++; bytes += e.size; }
    }
    sys_closedir(h);
    if (rc < 0) { printf("ls: readdir error %d\n", rc); return 1; }
    printf("%d files (%lu bytes), %d dirs\n", files, bytes, dirs);
    return 0;
}
