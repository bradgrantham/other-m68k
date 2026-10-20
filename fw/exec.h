/* ELF loading into user RAM and user-mode launch with argc/argv. */
#ifndef EXEC_H
#define EXEC_H
#include <stdint.h>
#include "elf.h"
#include "fs.h"

struct exec_image {
    char path[FS_PATH_MAX];
    uint32_t entry;
    uint32_t lowest, highest;           /* loaded address range (highest exclusive) */
    int nseg;
    struct elf_phdr seg[ELF_MAX_SEGS];
};

/* Open `path`, validate the ELF and program headers against user RAM; touches no user
 * memory. 0 or E_* (E_NOENT, E_NOEXEC, E_NOMEM, E_IO). */
int exec_prepare(const char *path, struct exec_image *img);
/* Copy the PT_LOAD segments into RAM and zero the bss tails. 0 or E_*; user RAM may be
 * partly overwritten when this fails. */
int exec_load(struct exec_image *img);
/* Build argv on the user stack (argv[0] = argv0, then args split on blanks) and enter the
 * program at entry in user mode. argv0/args may be NULL. Never returns. */
void user_launch(uint32_t entry, const char *argv0, const char *args) __attribute__((noreturn));

#define EXEC_MAX_ARGS 16

#endif
