/* libgloss-style system interface for newlib on other-m68k. This newlib build calls the
 * plain names (write, read, sbrk, ...), not _write etc. Everything maps onto TRAP #15
 * system calls from generated/syscall_stubs.S.
 *
 *   fd 0       console input  (read(): a small line editor with echo; the keyboard)
 *   fd 1, 2    console output (the video console) via sys_write
 *   fd >= 3    SD card files via sys_open/read/write/lseek/close (8.3 names, see fw/README.md),
 *              or devices: open("/dev/ttya") is serial port A, which the BIOS releases
 *              after the POST; those fds report isatty() so stdio line-buffers them
 *   sbrk       __heap_start .. mem_info.user_end - STACK_RESERVE
 *
 * newlib's O_* flag values (fcntl.h) differ from the BIOS ABI's (syscalls.h); open()
 * translates. Both headers define O_RDONLY etc., so newlib's are captured first.
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include "hw.h"                                 /* CPU_CLOCK_HZ, VIDEO_*_FRAME_CLOCKS */
enum { NL_O_ACCMODE = O_ACCMODE, NL_O_APPEND = O_APPEND, NL_O_CREAT = O_CREAT, NL_O_TRUNC = O_TRUNC };
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#undef O_APPEND
#undef O_CREAT
#undef O_TRUNC
#include "syscalls.h"

#define STACK_RESERVE (16 * 1024)

extern char __heap_start[];
char *__env[1] = { 0 };
char **environ = __env;

static int map_err(int r)
{
    if (r >= 0) return r;
    /* every E_* in syscalls.h; the ABI values are Linux numbers, newlib's differ for some */
    switch (r) {
    case E_NOENT: errno = ENOENT; break;
    case E_IO: errno = EIO; break;
    case E_NOEXEC: errno = ENOEXEC; break;
    case E_BADF: errno = EBADF; break;
    case E_NOMEM: errno = ENOMEM; break;
    case E_ACCES: errno = EACCES; break;
    case E_FAULT: errno = EFAULT; break;
    case E_EXIST: errno = EEXIST; break;
    case E_NODEV: errno = ENODEV; break;
    case E_NOTDIR: errno = ENOTDIR; break;
    case E_ISDIR: errno = EISDIR; break;
    case E_INVAL: errno = EINVAL; break;
    case E_NFILE: errno = ENFILE; break;
    case E_NOSPC: errno = ENOSPC; break;
    case E_ROFS: errno = EROFS; break;
    case E_NAMETOOLONG: errno = ENAMETOOLONG; break;
    case E_NOSYS: errno = ENOSYS; break;
    default: errno = ENOSYS; break;         /* not in the ABI */
    }
    return -1;
}

void _exit(int code)
{
    for (;;) sys_exit(code);
}

int write(int fd, const void *buf, size_t len)
{
    return map_err(sys_write(fd, (unsigned)len, buf));
}

int read(int fd, void *buf, size_t len)
{
    if (fd != 0)
        return map_err(sys_read(fd, (unsigned)len, buf));
    /* console: line editor, returns the line including '\n' */
    char *p = buf;
    size_t n = 0;
    for (;;) {
        int c = sys_getchar();
        if (c == '\r' || c == '\n') {
            sys_putchar('\n');
            if (n < len) p[n++] = '\n';
            return (int)n;
        }
        if (c == '\b' || c == 0x7F) {
            if (n) { n--; sys_puts("\b \b"); }
            continue;
        }
        if (c < 0x20 || c > 0x7E) continue;
        if (n + 1 < len) { p[n++] = (char)c; sys_putchar(c); }
    }
}

void *sbrk(ptrdiff_t incr)
{
    static char *brk;
    static char *limit;
    if (!limit) {
        struct mem_info mi;
        brk = __heap_start;
        if (sys_mem_info(&mi) == 0) limit = (char *)(mi.user_end - STACK_RESERVE);
        else limit = brk;
    }
    if (brk + incr > limit || brk + incr < __heap_start) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *old = brk;
    brk += incr;
    return old;
}

static unsigned dev_fds;                        /* bit n: fd n was opened as a /dev/ device */

int open(const char *path, int flags, ...)
{
    int f = flags & NL_O_ACCMODE;               /* O_RDONLY/O_WRONLY/O_RDWR are 0/1/2 in both */
    if (flags & NL_O_CREAT) f |= O_CREAT;
    if (flags & NL_O_TRUNC) f |= O_TRUNC;
    if (flags & NL_O_APPEND) f |= O_APPEND;
    int fd = map_err(sys_open(f, path));
    if (fd >= 0 && fd < 32) {
        if (!strncmp(path, "/dev/", 5)) dev_fds |= 1u << fd;
        else dev_fds &= ~(1u << fd);
    }
    return fd;
}

int close(int fd)
{
    if (fd < 3) return 0;
    if (fd < 32) dev_fds &= ~(1u << fd);
    return map_err(sys_close(fd));
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (fd < 3) { errno = ESPIPE; return -1; }
    return map_err(sys_lseek(fd, (int)offset, whence));
}

int fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof *st);
    if (fd < 3) { st->st_mode = S_IFCHR; st->st_blksize = 0; return 0; }
    int cur = sys_lseek(fd, 0, SEEK_CUR);
    if (cur < 0) return map_err(cur);
    int end = sys_lseek(fd, 0, SEEK_END);
    sys_lseek(fd, cur, SEEK_SET);
    st->st_mode = S_IFREG | 0644;
    st->st_size = end < 0 ? 0 : end;
    st->st_blksize = 512;
    st->st_nlink = 1;
    return 0;
}

/* The BIOS has no stat call; open the file (or, failing that, the directory). */
int stat(const char *path, struct stat *st)
{
    int fd = sys_open(O_RDONLY, path);
    if (fd >= 0) {
        int r = fstat(fd, st);
        sys_close(fd);
        return r;
    }
    int h = sys_opendir(path);
    if (h >= 0) {
        sys_closedir(h);
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 1;
        return 0;
    }
    return map_err(fd);
}

int isatty(int fd)
{
    return (fd >= 0 && fd < 3) || (fd >= 3 && fd < 32 && (dev_fds >> fd) & 1);
}

int kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = ENOSYS;
    return -1;
}

int getpid(void)
{
    return 1;
}

int link(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
int unlink(const char *a) { (void)a; errno = ENOSYS; return -1; }

/* sys_ticks() counts frames. The frame length in CPU clocks comes from the video standard
 * the BIOS selected at boot (video_info.standard; it cannot change while a program runs):
 * 764 x 262 clocks on NTSC (59.95 Hz), 768 x 312 on PAL (50.08 Hz), at CPU_CLOCK_HZ. */
static uint32_t frame_clocks(void)
{
    static uint32_t fc;
    if (!fc) {
        struct video_info vi;
        fc = (sys_video_info(&vi) == 0 && vi.standard == VIDEO_STD_PAL)
            ? VIDEO_PAL_FRAME_CLOCKS : VIDEO_NTSC_FRAME_CLOCKS;
    }
    return fc;
}

/* CPU clocks since boot, at frame resolution (64 bits: 32 overflow after ~6 minutes) */
static uint64_t uptime_clocks(void)
{
    return (uint64_t)sys_ticks() * frame_clocks();
}

/* newlib's clock() is times() utime + stime, divided by CLOCKS_PER_SEC (1000) by the
 * caller, so times() counts milliseconds here (POSIX leaves the unit to the system). */
clock_t times(struct tms *t)
{
    clock_t ms = (clock_t)(uptime_clocks() / (CPU_CLOCK_HZ / CLOCKS_PER_SEC));
    if (t) { t->tms_utime = ms; t->tms_stime = 0; t->tms_cutime = 0; t->tms_cstime = 0; }
    return ms;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        uint64_t c = uptime_clocks();
        tv->tv_sec = (time_t)(c / CPU_CLOCK_HZ);
        tv->tv_usec = (suseconds_t)((uint32_t)(c % CPU_CLOCK_HZ) / (CPU_CLOCK_HZ / 1000000));
    }
    return 0;
}

/* Referenced by __libc_init_array if anything pulls it in; we have no crti/crtn. */
void _init(void) {}
void _fini(void) {}
