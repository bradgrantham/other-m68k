/* File descriptor table and POSIX-ish semantics over FatFs R0.15 (8.3 names, one volume,
 * tiny mode: all partial-sector traffic goes through the FATFS window), plus the /dev/
 * devices, which share the descriptor slots. */
#include "bios.h"
#include "fs.h"
#include "sd.h"
#include "ff.h"

static FATFS fatfs;
static int fs_mounted;

enum { DEV_NONE = 0, DEV_TTYA };
struct fs_file { FIL fil; uint8_t used; uint8_t append; uint8_t mode; uint8_t dev; };
struct fs_dir  { DIR dir; uint8_t used; };
static struct fs_file files[FS_NFILES];
static struct fs_dir dirs[FS_NDIRS];

/* ---- FRESULT -> ABI error --------------------------------------------------------- */

static int fs_err(FRESULT r)
{
    switch (r) {
    case FR_OK: return 0;
    case FR_NO_FILE: case FR_NO_PATH: return E_NOENT;
    case FR_INVALID_NAME: return E_NAMETOOLONG;   /* not an 8.3 name */
    case FR_INVALID_DRIVE: case FR_INVALID_PARAMETER: return E_INVAL;
    case FR_EXIST: return E_EXIST;
    case FR_DENIED: return E_NOSPC;         /* directory full / no free cluster (our own checks cover the rest) */
    case FR_INVALID_OBJECT: return E_BADF;
    case FR_TOO_MANY_OPEN_FILES: return E_NFILE;
    case FR_NOT_ENOUGH_CORE: return E_NOMEM;
    case FR_NOT_READY: case FR_NO_FILESYSTEM:
        fs_invalidate();                    /* no card / no FAT volume: retry from scratch next time */
        return E_NODEV;
    case FR_DISK_ERR:
        fs_invalidate();
        return E_IO;
    default: return E_IO;                   /* INT_ERR, WRITE_PROTECTED, NOT_ENABLED, MKFS, TIMEOUT, LOCKED */
    }
}

const char *fs_strerror(int err)
{
    switch (err) {
    case 0: return "ok";
    case E_NOENT: return "not found";
    case E_IO: return "I/O error";
    case E_NODEV: return "no card or no FAT volume";
    case E_NAMETOOLONG: return "not an 8.3 name";
    case E_FAULT: return "bad address";
    case E_NOEXEC: return "not a runnable ELF";
    case E_BADF: return "bad descriptor";
    case E_NOMEM: return "does not fit in user RAM";
    case E_EXIST: return "exists";
    case E_NOTDIR: return "not a directory";
    case E_ISDIR: return "is a directory";
    case E_INVAL: return "invalid argument";
    case E_NFILE: return "too many open files";
    case E_NOSPC: return "no space";
    case E_NOSYS: return "not implemented";
    default: return "error";
    }
}

/* ---- mount ------------------------------------------------------------------------- */

int fs_mount(void)
{
    if (fs_mounted) return 0;
    FRESULT r = f_mount(&fatfs, "", 1);
    if (r != FR_OK) return fs_err(r);
    fs_mounted = 1;
    return 0;
}

void fs_invalidate(void)
{
    fs_mounted = 0;
    sd_card.ready = 0;
    for (int i = 0; i < FS_NFILES; i++) if (!files[i].dev) files[i].used = 0;   /* devices survive card errors */
    for (int i = 0; i < FS_NDIRS; i++) dirs[i].used = 0;
}

/* ---- files --------------------------------------------------------------------------- */

static struct fs_file *fs_file(int fd)
{
    int i = fd - FS_FD_BASE;
    if (i < 0 || i >= FS_NFILES || !files[i].used) return 0;
    return &files[i];
}

/* ---- devices ------------------------------------------------------------------------- */

static int dev_lookup(const char *path)
{
    if (strncmp(path, "/dev/", 5)) return DEV_NONE;
    if (!strcmp(path + 5, "ttya")) return DEV_TTYA;
    return -1;                              /* a /dev/ name we do not have */
}

/* /dev/ttya read: like the keyboard on fd 0, block for the first byte and then return
 * whatever else the level-3 ISR has already put in the ring. */
static int dev_read(int dev, uint8_t *p, unsigned len)
{
    (void)dev;
    unsigned n = 0;
    if (!len) return 0;
    int c;
    while ((c = serial_getc()) < 0) ;
    p[n++] = (uint8_t)c;
    while (n < len && (c = serial_getc()) >= 0) p[n++] = (uint8_t)c;
    return (int)n;
}

static int dev_write(int dev, const uint8_t *p, unsigned len)
{
    (void)dev;
    for (unsigned i = 0; i < len; i++) serial_putc(p[i]);    /* raw: no newline translation */
    return (int)len;
}

/* ---- files --------------------------------------------------------------------------- */

int fs_open(int flags, const char *path)
{
    int acc = flags & 3;
    if (acc == 3) return E_INVAL;
    int dev = dev_lookup(path);
    if (dev < 0) return E_NOENT;
    int i;
    if (dev) {                              /* a device: no card needed, create/trunc/append ignored */
        for (i = 0; i < FS_NFILES && files[i].used; i++) ;
        if (i == FS_NFILES) return E_NFILE;
        files[i].used = 1;
        files[i].dev = (uint8_t)dev;
        files[i].append = 0;
        files[i].mode = (acc == O_WRONLY) ? FA_WRITE : (acc == O_RDWR) ? FA_READ | FA_WRITE : FA_READ;
        return FS_FD_BASE + i;
    }
    int rc = fs_mount();
    if (rc) return rc;
    for (i = 0; i < FS_NFILES && files[i].used; i++) ;
    if (i == FS_NFILES) return E_NFILE;

    BYTE mode = (acc == O_WRONLY) ? FA_WRITE : (acc == O_RDWR) ? FA_READ | FA_WRITE : FA_READ;
    if (flags & (O_CREAT | O_TRUNC | O_APPEND)) {
        if (acc == O_RDONLY) return E_INVAL;
        if (flags & O_CREAT) mode |= (flags & O_TRUNC) ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
        else mode |= FA_OPEN_EXISTING;
    }
    struct fs_file *f = &files[i];
    FRESULT r = f_open(&f->fil, path, mode);
    if (r != FR_OK) return fs_err(r);
    if ((flags & O_TRUNC) && !(flags & O_CREAT)) {
        r = f_truncate(&f->fil);
        if (r != FR_OK) { f_close(&f->fil); return fs_err(r); }
    }
    f->used = 1;
    f->append = (flags & O_APPEND) ? 1 : 0;
    f->mode = mode;
    f->dev = DEV_NONE;
    return FS_FD_BASE + i;
}

int fs_close(int fd)
{
    struct fs_file *f = fs_file(fd);
    if (!f) return E_BADF;
    f->used = 0;
    if (f->dev) return 0;
    FRESULT r = f_close(&f->fil);
    return fs_err(r);
}

int fs_read(int fd, void *buf, unsigned len)
{
    struct fs_file *f = fs_file(fd);
    if (!f) return E_BADF;
    if (!(f->mode & FA_READ)) return E_BADF;
    if (f->dev) return dev_read(f->dev, buf, len);
    UINT got = 0;
    FRESULT r = f_read(&f->fil, buf, len, &got);
    if (r != FR_OK) return fs_err(r);
    return (int)got;
}

int fs_write(int fd, const void *buf, unsigned len)
{
    struct fs_file *f = fs_file(fd);
    if (!f) return E_BADF;
    if (!(f->mode & FA_WRITE)) return E_BADF;
    if (f->dev) return dev_write(f->dev, buf, len);
    FRESULT r;
    if (f->append) {
        r = f_lseek(&f->fil, f_size(&f->fil));
        if (r != FR_OK) return fs_err(r);
    }
    UINT put = 0;
    r = f_write(&f->fil, buf, len, &put);
    if (r != FR_OK) return fs_err(r);
    if (put < len) return E_NOSPC;
    return (int)put;
}

int fs_lseek(int fd, int offset, int whence)
{
    struct fs_file *f = fs_file(fd);
    if (!f) return E_BADF;
    if (f->dev) return E_INVAL;                 /* a stream is not seekable */
    int32_t base;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = (int32_t)f_tell(&f->fil); break;
    case SEEK_END: base = (int32_t)f_size(&f->fil); break;
    default: return E_INVAL;
    }
    int32_t pos = base + offset;
    if (pos < 0) return E_INVAL;
    FRESULT r = f_lseek(&f->fil, (FSIZE_t)pos);
    if (r != FR_OK) return fs_err(r);
    return (int)f_tell(&f->fil);
}

int fs_size(int fd)
{
    struct fs_file *f = fs_file(fd);
    if (!f) return E_BADF;
    if (f->dev) return E_INVAL;
    return (int)f_size(&f->fil);
}

/* ---- directories ------------------------------------------------------------------------ */

int fs_opendir(const char *path)
{
    int rc = fs_mount();
    if (rc) return rc;
    int i;
    for (i = 0; i < FS_NDIRS && dirs[i].used; i++) ;
    if (i == FS_NDIRS) return E_NFILE;
    FRESULT r = f_opendir(&dirs[i].dir, path);
    if (r != FR_OK) return fs_err(r);
    dirs[i].used = 1;
    return i;
}

int fs_readdir(int handle, struct dirent *ent)
{
    if (handle < 0 || handle >= FS_NDIRS || !dirs[handle].used) return E_BADF;
    FILINFO fno;
    FRESULT r = f_readdir(&dirs[handle].dir, &fno);
    if (r != FR_OK) return fs_err(r);
    if (fno.fname[0] == 0) return 0;
    memset(ent, 0, sizeof *ent);
    strcpy(ent->name, fno.fname);           /* 8.3: at most 12 chars + NUL */
    ent->size = fno.fsize;
    ent->attr = 0;
    if (fno.fattrib & AM_DIR) ent->attr |= ATTR_DIR;
    if (fno.fattrib & AM_RDO) ent->attr |= ATTR_READONLY;
    if (fno.fattrib & AM_HID) ent->attr |= ATTR_HIDDEN;
    return 1;
}

int fs_closedir(int handle)
{
    if (handle < 0 || handle >= FS_NDIRS || !dirs[handle].used) return E_BADF;
    FRESULT r = f_closedir(&dirs[handle].dir);
    dirs[handle].used = 0;
    return fs_err(r);
}

void fs_close_all(void)
{
    for (int i = 0; i < FS_NFILES; i++)
        if (files[i].used) { if (!files[i].dev) f_close(&files[i].fil); files[i].used = 0; }
    for (int i = 0; i < FS_NDIRS; i++)
        if (dirs[i].used) { f_closedir(&dirs[i].dir); dirs[i].used = 0; }
}
