/* ELF loader and user-mode launch.
 *
 * Launch convention (shared with sdk/crt0.S): the user stack top holds the argument
 * strings and the argv array, below them the frame of a call `_start(argc, argv)`:
 *
 *      USP ->  return address = user_exit_stub (a bare `rts` becomes sys_exit(0))
 *              argc
 *              argv            -> { argv[0], ..., argv[argc-1], NULL }
 *
 * argv[0] is the path as given to `run`/`exec`; the remaining words of the argument
 * string are split on blanks, double quotes grouping a word (args.c). `go` launches with
 * argc = 0 and argv[0] == NULL. */
#include "bios.h"
#include "exec.h"
#include "args.h"

extern void user_exit_stub(void);       /* crt0.S */

int exec_prepare(const char *path, struct exec_image *img)
{
    uint8_t hdr[ELF_HDR_SIZE];
    struct elf_hdr_info hi;
    int rc, fd;

    memset(img, 0, sizeof *img);
    if (strlen(path) >= FS_PATH_MAX) return E_INVAL;
    strcpy(img->path, path);
    fd = fs_open(O_RDONLY, path);
    if (fd < 0) return fd;
    rc = fs_read(fd, hdr, sizeof hdr);
    if (rc < 0) goto out;
    rc = elf_parse_header(hdr, (uint32_t)rc, &hi);
    if (rc) goto out;
    img->entry = hi.entry;
    img->lowest = 0xFFFFFFFF;
    for (unsigned i = 0; i < hi.phnum; i++) {
        uint8_t ph[ELF_PHDR_SIZE];
        struct elf_phdr p;
        rc = fs_lseek(fd, (int)(hi.phoff + i * ELF_PHDR_SIZE), SEEK_SET);
        if (rc < 0) goto out;
        rc = fs_read(fd, ph, sizeof ph);
        if (rc < 0) goto out;
        if (rc != ELF_PHDR_SIZE) { rc = E_NOEXEC; goto out; }
        elf_parse_phdr(ph, &p);
        if (p.type != ELF_PT_LOAD || p.memsz == 0) continue;
        if (!elf_segment_fits(&p)) { rc = E_NOMEM; goto out; }
        if (img->nseg == ELF_MAX_SEGS) { rc = E_NOEXEC; goto out; }
        img->seg[img->nseg++] = p;
        if (p.vaddr < img->lowest) img->lowest = p.vaddr;
        if (p.vaddr + p.memsz > img->highest) img->highest = p.vaddr + p.memsz;
    }
    if (img->nseg == 0 || img->entry < img->lowest || img->entry >= img->highest) rc = E_NOEXEC;
    else rc = 0;
out:
    fs_close(fd);
    return rc;
}

int exec_load(struct exec_image *img)
{
    int fd = fs_open(O_RDONLY, img->path);
    int rc = 0;
    if (fd < 0) return fd;
    for (int i = 0; i < img->nseg && rc == 0; i++) {
        const struct elf_phdr *p = &img->seg[i];
        if (p->filesz) {
            rc = fs_lseek(fd, (int)p->offset, SEEK_SET);
            if (rc < 0) break;
            rc = fs_read(fd, (void *)(uintptr_t)p->vaddr, p->filesz);
            if (rc < 0) break;
            if ((uint32_t)rc != p->filesz) { rc = E_NOEXEC; break; }     /* truncated file */
            rc = 0;
        }
        if (p->memsz > p->filesz)
            memset((void *)(uintptr_t)(p->vaddr + p->filesz), 0, p->memsz - p->filesz);
    }
    fs_close(fd);
    return rc;
}

void user_launch(uint32_t entry, const char *argv0, const char *args)
{
    char *sp = (char *)(uintptr_t)USER_STACK_TOP;
    char *argv[EXEC_MAX_ARGS + 1];
    int argc = 0;

    if (argv0) {
        sp -= strlen(argv0) + 1;
        strcpy(sp, argv0);
        argv[argc++] = sp;
    }
    if (args) {
        sp -= strlen(args) + 1;
        strcpy(sp, args);
        argc += args_split(sp, argv + argc, 0, EXEC_MAX_ARGS - argc);
    }
    argv[argc] = 0;
    sp = (char *)((uintptr_t)sp & ~3u);
    uint32_t *w = (uint32_t *)sp;
    w -= argc + 1;
    for (int i = 0; i <= argc; i++) w[i] = (uint32_t)(uintptr_t)argv[i];
    uint32_t argv_addr = (uint32_t)(uintptr_t)w;
    *--w = argv_addr;
    *--w = (uint32_t)argc;
    *--w = (uint32_t)(uintptr_t)user_exit_stub;
    run_user(entry, (uint32_t)(uintptr_t)w);
}
