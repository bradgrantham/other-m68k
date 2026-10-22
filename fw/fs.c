/* File descriptor table and POSIX-ish semantics over FatFs R0.15 (two volumes, long file
 * names up to 63 characters, relative paths, tiny mode: all partial-sector traffic goes
 * through each volume's FATFS window), plus the /dev/ devices, which share the descriptor
 * slots. */
#include "bios.h"
#include "fs.h"
#include "sd.h"
#include "clock.h"
#include "flashdisk.h"
#include "ff.h"

static FATFS fatfs[FF_VOLUMES];         /* [FS_PDRV_FLASH] = C:, [FS_PDRV_SD] = D: */

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
    case FR_INVALID_NAME: return E_NAMETOOLONG;   /* malformed or longer than 63 characters */
    case FR_INVALID_DRIVE: case FR_INVALID_PARAMETER: return E_INVAL;
    case FR_EXIST: return E_EXIST;
    case FR_DENIED: return E_NOSPC;         /* directory full / no free cluster (callers map the other causes) */
    case FR_INVALID_OBJECT: return E_BADF;
    case FR_TOO_MANY_OPEN_FILES: return E_NFILE;
    case FR_NOT_ENOUGH_CORE: return E_NOMEM;
    case FR_WRITE_PROTECTED: return E_ACCES;
    case FR_NOT_READY: case FR_NO_FILESYSTEM:
        fs_invalidate();                    /* no card / no FAT volume: retry from scratch next time */
        return E_NODEV;
    case FR_DISK_ERR:
        fs_invalidate();
        return E_IO;
    default: return E_IO;                   /* INT_ERR, NOT_ENABLED, MKFS, TIMEOUT, LOCKED */
    }
}

const char *fs_strerror(int err)
{
    switch (err) {
    case 0: return "ok";
    case E_NOENT: return "not found";
    case E_IO: return "I/O error";
    case E_NODEV: return "volume not mounted (no card, or not formatted)";
    case E_NAMETOOLONG: return "bad name (max 63 chars)";
    case E_FAULT: return "bad address";
    case E_NOEXEC: return "not a runnable ELF";
    case E_BADF: return "bad descriptor";
    case E_NOMEM: return "does not fit in user RAM";
    case E_EXIST: return "exists / not empty";
    case E_NOTDIR: return "not a directory";
    case E_ISDIR: return "is a directory";
    case E_INVAL: return "invalid argument";
    case E_NFILE: return "too many open files";
    case E_NOSPC: return "no space";
    case E_ACCES: return "read-only";
    case E_NOSYS: return "not implemented";
    default: return "error";
    }
}

/* ---- volumes ------------------------------------------------------------------------- */

void fs_init(void)
{
    f_mount(&fatfs[FS_PDRV_FLASH], "C:", 0);    /* deferred: mounted on first access */
    f_mount(&fatfs[FS_PDRV_SD], "D:", 0);
}

/* The card is gone or answered wrongly: FatFs re-initialises the drive and re-reads the
 * volume on the next access once disk_status reports STA_NOINIT (sd_card.ready = 0), and
 * its mount id check makes every open object on D: fail with FR_INVALID_OBJECT; drop the
 * slots that point there so the fds read as closed. C: and the devices are untouched. */
void fs_invalidate(void)
{
    sd_card.ready = 0;
    for (int i = 0; i < FS_NFILES; i++)
        if (files[i].used && !files[i].dev && files[i].fil.obj.fs == &fatfs[FS_PDRV_SD]) files[i].used = 0;
    for (int i = 0; i < FS_NDIRS; i++)
        if (dirs[i].used && dirs[i].dir.obj.fs == &fatfs[FS_PDRV_SD]) dirs[i].used = 0;
}

int fs_sync(void)
{
    return flashdisk_flush() ? E_IO : 0;
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
    for (i = 0; i < FS_NFILES && files[i].used; i++) ;
    if (i == FS_NFILES) return E_NFILE;
    if (dev) {                              /* a device: no card needed, create/trunc/append ignored */
        files[i].used = 1;
        files[i].dev = (uint8_t)dev;
        files[i].append = 0;
        files[i].mode = (acc == O_WRONLY) ? FA_WRITE : (acc == O_RDWR) ? FA_READ | FA_WRITE : FA_READ;
        return FS_FD_BASE + i;
    }
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

static void fill_dirent(struct dirent *ent, const FILINFO *fno)
{
    memset(ent, 0, sizeof *ent);
    strcpy(ent->name, fno->fname);          /* the long name, or the 8.3 name if there is none (<= 63 + NUL) */
    ent->size = fno->fsize;
    ent->attr = 0;
    if (fno->fattrib & AM_DIR) ent->attr |= ATTR_DIR;
    if (fno->fattrib & AM_RDO) ent->attr |= ATTR_READONLY;
    if (fno->fattrib & AM_HID) ent->attr |= ATTR_HIDDEN;
    ent->mtime = clock_from_fat(fno->fdate, fno->ftime);
}

int fs_opendir(const char *path)
{
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
    fill_dirent(ent, &fno);
    return 1;
}

int fs_closedir(int handle)
{
    if (handle < 0 || handle >= FS_NDIRS || !dirs[handle].used) return E_BADF;
    FRESULT r = f_closedir(&dirs[handle].dir);
    dirs[handle].used = 0;
    return fs_err(r);
}

/* ---- names ------------------------------------------------------------------------------- */

/* The root of a volume ("", "/", "C:", "C:/") is not a directory entry FatFs can stat; treat
 * it as an existing directory. */
static int is_root(const char *path)
{
    if (path[0] && path[1] == ':') path += 2;
    return path[0] == 0 || ((path[0] == '/' || path[0] == '\\') && path[1] == 0);
}

int fs_stat(const char *path, struct dirent *ent)
{
    if (dev_lookup(path)) return E_NOENT;
    if (is_root(path)) {
        if (ent) { memset(ent, 0, sizeof *ent); ent->attr = ATTR_DIR; }
        return 0;
    }
    FILINFO fno;
    FRESULT r = f_stat(path, &fno);
    if (r != FR_OK) return fs_err(r);
    if (ent) fill_dirent(ent, &fno);
    return 0;
}

int fs_unlink(const char *path)
{
    FRESULT r = f_unlink(path);
    if (r == FR_DENIED) {                   /* read-only entry, or a directory that is not empty */
        FILINFO fno;
        if (f_stat(path, &fno) == FR_OK && (fno.fattrib & AM_RDO)) return E_ACCES;
        return E_EXIST;
    }
    return fs_err(r);
}

int fs_rename(const char *oldp, const char *newp)
{
    return fs_err(f_rename(oldp, newp));
}

int fs_mkdir(const char *path)
{
    return fs_err(f_mkdir(path));
}

int fs_chdir(const char *path)
{
    if (path[0] && path[1] == ':' && path[2] == 0) return fs_err(f_chdrive(path));
    return fs_err(f_chdir(path));
}

int fs_getcwd(char *buf, unsigned len)
{
    FRESULT r = f_getcwd(buf, len);
    if (r == FR_NOT_ENOUGH_CORE) return E_INVAL;
    if (r != FR_OK) return fs_err(r);
    return (int)strlen(buf);
}

/* ---- copy -------------------------------------------------------------------------------- */

int fs_copy(const char *src, const char *dst, uint8_t *buf, unsigned buflen)
{
    FIL in, out;
    FILINFO fno;
    FRESULT r = f_stat(src, &fno);
    if (r != FR_OK) return fs_err(r);
    if (fno.fattrib & AM_DIR) return E_ISDIR;
    r = f_open(&in, src, FA_READ);
    if (r != FR_OK) return fs_err(r);
    r = f_open(&out, dst, FA_WRITE | FA_CREATE_ALWAYS);
    if (r != FR_OK) { f_close(&in); return fs_err(r); }
    uint32_t total = 0;
    int rc = 0;
    for (;;) {
        UINT got = 0, put = 0;
        r = f_read(&in, buf, buflen, &got);
        if (r != FR_OK) { rc = fs_err(r); break; }
        if (!got) break;
        r = f_write(&out, buf, got, &put);
        if (r != FR_OK) { rc = fs_err(r); break; }
        if (put < got) { rc = E_NOSPC; break; }
        total += got;
    }
    f_close(&in);
    r = f_close(&out);
    if (!rc && r != FR_OK) rc = fs_err(r);
    if (rc) { f_unlink(dst); return rc; }   /* no partial files (a full volume is the usual cause) */
    f_utime(dst, &fno);                     /* keep the source's stamp */
    return (int)total;
}

/* ---- format ------------------------------------------------------------------------------ */

int fs_mkfs(const char *drive, uint8_t *buf, unsigned buflen)
{
    if (!((drive[0] | 0x20) == 'c' && drive[1] == ':' && drive[2] == 0)) return E_INVAL;
    if (buflen < FF_MAX_SS) return E_INVAL;
    fs_close_all();
    /* FAT12, no partition table, two FATs, 224 root entries, 4 KB clusters, data area
     * aligned to the 8 KB erase sector (spec flash.volume_desc) */
    MKFS_PARM p = { FM_FAT | FM_SFD, 2, FLASHDISK_BLOCK, 224, 4096 };
    FRESULT r = f_mkfs("C:", &p, buf, buflen);
    if (r == FR_MKFS_ABORTED) return E_INVAL;
    if (r != FR_OK) return fs_err(r);
    f_mount(&fatfs[FS_PDRV_FLASH], "C:", 0);       /* forget the old (unmountable) state */
    return fs_sync();
}

void fs_close_all(void)
{
    for (int i = 0; i < FS_NFILES; i++)
        if (files[i].used) { if (!files[i].dev) f_close(&files[i].fil); files[i].used = 0; }
    for (int i = 0; i < FS_NDIRS; i++)
        if (dirs[i].used) { f_closedir(&dirs[i].dir); dirs[i].used = 0; }
    flashdisk_flush();
}
