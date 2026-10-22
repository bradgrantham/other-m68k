/* ls: list a directory with the BIOS directory calls, and show how argv arrives.
 * `ls` lists the current directory, `ls D:/GAMES` another one; names are long names,
 * stamps are the BIOS clock's (blank when the entry has none). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "syscalls.h"

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "";
    struct dirent e;
    char cwd[128], stamp[20];
    int h, rc, files = 0, dirs = 0;
    unsigned long bytes = 0;

    printf("argc %d", argc);
    for (int i = 0; i < argc; i++) printf(" [%s]", argv[i]);
    printf("\n");
    if (sys_getcwd(sizeof cwd, cwd) > 0) printf("cwd %s\n", cwd);

    h = sys_opendir(path);
    if (h < 0) { printf("ls: %s: error %d\n", path, h); return 1; }
    while ((rc = sys_readdir(h, &e)) > 0) {
        if (e.mtime) {
            time_t t = (time_t)e.mtime;
            strftime(stamp, sizeof stamp, "%y-%m-%d %H:%M", gmtime(&t));   /* local time, no zone */
        } else strcpy(stamp, "");
        if (strlen(e.name) > 16) printf("%s\n%16s", e.name, "");
        else printf("%-16s", e.name);
        if (e.attr & ATTR_DIR) { printf("   <DIR> %s\n", stamp); dirs++; }
        else { printf("%8lu %s%s\n", (unsigned long)e.size, stamp, (e.attr & ATTR_READONLY) ? " R" : ""); files++; bytes += e.size; }
    }
    sys_closedir(h);
    if (rc < 0) { printf("ls: readdir error %d\n", rc); return 1; }
    printf("%d files (%lu bytes), %d dirs\n", files, bytes, dirs);
    return 0;
}
