/* FatFs + the fd table (fs.c) on the host: drive D: is a FAT32 image file through a host
 * diskio, drive C: is a FAT12 image loaded into a RAM array that stands in for the flash
 * (flashdisk.c runs for real on it; flash_write_sector is simulated: erase to 0xFF, then
 * program, counting the cycles). */
#include "test.h"
#include "fs.h"
#include "sd.h"
#include "clock.h"
#include "flashdisk.h"
#include "ff.h"
#include "diskio.h"

static FILE *img;
static long img_blocks;
static int flash_writes;

/* ---- the flash array ---- */
int flash_write_sector(void *dst, const uint16_t *src)
{
    uint8_t *p = dst;
    if (p < flashdisk_base || p + FLASH_SECTOR_BYTES > flashdisk_base + FLASH_VOL_SIZE || ((p - flashdisk_base) % FLASH_SECTOR_BYTES)) {
        printf("flash_write_sector: bad dst %p\n", (void *)p); return 3;
    }
    memset(p, 0xFF, FLASH_SECTOR_BYTES);                /* erase */
    for (unsigned i = 0; i < FLASH_SECTOR_BYTES; i++) p[i] &= ((const uint8_t *)src)[i];   /* program clears bits */
    flash_writes++;
    return memcmp(p, src, FLASH_SECTOR_BYTES) ? 3 : 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv == FS_PDRV_FLASH) return 0;
    return (pdrv == FS_PDRV_SD && img && sd_card.ready) ? 0 : STA_NOINIT;
}
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv == FS_PDRV_FLASH) return 0;
    if (pdrv != FS_PDRV_SD || !img) return STA_NOINIT;
    sd_card.ready = 1; sd_card.blocks = (uint32_t)img_blocks;   /* stands in for sd_init */
    return 0;
}
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == FS_PDRV_FLASH) return flashdisk_read((uint32_t)sector, buff, count) ? RES_PARERR : RES_OK;
    if (pdrv != FS_PDRV_SD || !img) return RES_NOTRDY;
    if (fseek(img, (long)sector * 512, SEEK_SET)) return RES_ERROR;
    return fread(buff, 512, count, img) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == FS_PDRV_FLASH) return flashdisk_write((uint32_t)sector, buff, count) ? RES_ERROR : RES_OK;
    if (pdrv != FS_PDRV_SD || !img) return RES_NOTRDY;
    if (fseek(img, (long)sector * 512, SEEK_SET)) return RES_ERROR;
    return fwrite(buff, 512, count, img) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv == FS_PDRV_FLASH) {
        switch (cmd) {
        case CTRL_SYNC: return flashdisk_flush() ? RES_ERROR : RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)buff = FLASHDISK_SECTORS; return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD *)buff = FLASHDISK_BLOCK; return RES_OK;
        default: return RES_PARERR;
        }
    }
    if (pdrv != FS_PDRV_SD) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC: fflush(img); return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = (LBA_t)img_blocks; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

/* The name as stored (readdir) of the entry that fs_stat finds under `name`: compares sizes
 * and stamps, since FatFs reports a looked-up name as it was typed. 1 found, 0 not. */
static int stored_name(const char *dir, const char *name, char *out)
{
    struct dirent want, e;
    char path[FS_PATH_MAX];
    strcpy(path, dir); strcat(path, name);
    if (fs_stat(path, &want)) return 0;
    int h = fs_opendir(dir[0] ? dir : ""), found = 0;
    if (h < 0) return 0;
    while (fs_readdir(h, &e) > 0)
        if (e.size == want.size && e.mtime == want.mtime && e.attr == want.attr && !found) { strcpy(out, e.name); found = 1; }
    fs_closedir(h);
    return found;
}

/* ---- drive C: (flash volume) ---- */
static void test_flash_volume(const char *cimg)
{
    char buf[1024];
    struct dirent e;
    int fd, rc;

    flashdisk_base = malloc(FLASH_VOL_SIZE);
    memset(flashdisk_base, 0xFF, FLASH_VOL_SIZE);
    FILE *f = cimg ? fopen(cimg, "rb") : 0;
    if (!f) { printf("test_fs: no flash volume image (%s), C: starts unformatted\n", cimg ? cimg : "none"); }
    else { size_t n = fread(flashdisk_base, 1, FLASH_VOL_SIZE, f); fclose(f); CHECK(n > 0); }

    fs_init();
    rc = fs_getcwd(buf, sizeof buf); CHECK_EQ(rc, 3); CHECK_STR(buf, "C:/");      /* C: is the current drive */
    if (!f) {                                                     /* unformatted: format it here */
        CHECK_EQ(fs_opendir("C:/"), E_NODEV);
        CHECK_EQ(fs_mkfs("C:", (uint8_t *)buf, sizeof buf), 0);
    } else {
        CHECK_EQ(fs_stat("C:/README.TXT", &e), 0); CHECK_EQ(e.size, 33);   /* put there by mkflashvol.sh */
        CHECK_EQ(fs_stat("README.TXT", &e), 0);                            /* relative: current drive, root */
    }
    CHECK_EQ(fs_stat("C:", &e), 0); CHECK(e.attr & ATTR_DIR);            /* the root */
    CHECK_EQ(fs_stat("/", &e), 0);
    CHECK_EQ(fs_stat("C:/NOPE.TXT", &e), E_NOENT);
    CHECK_EQ(fs_mkfs("D:", (uint8_t *)buf, sizeof buf), E_INVAL);

    /* long names: case kept, matched without case, 8.3 alias also opens it */
    clock_set(1792618200u);                                      /* 2026-10-21 21:30:00 */
    fd = fs_open(O_WRONLY | O_CREAT | O_TRUNC, "C:/A long file name.txt"); CHECK(fd >= FS_FD_BASE);
    CHECK_EQ(fs_write(fd, "lfn", 3), 3);
    CHECK_EQ(fs_close(fd), 0);
    CHECK_EQ(fs_stat("c:/a LONG FILE name.TXT", &e), 0);         /* (fs_stat reports the name as looked up) */
    CHECK_EQ(e.size, 3); CHECK_EQ(e.mtime, 1792618200u);
    CHECK_EQ(fs_stat("C:/ALONGF~1.TXT", &e), 0); CHECK_EQ(e.size, 3);   /* the 8.3 alias */
    CHECK_EQ(stored_name("C:/", "ALONGF~1.TXT", buf), 1); CHECK_STR(buf, "A long file name.txt");   /* readdir: as stored */
    memset(buf, 'x', 64); buf[64] = 0;
    CHECK_EQ(fs_open(O_WRONLY | O_CREAT, buf), E_NAMETOOLONG);   /* 64 characters: one too many */
    buf[63] = 0;
    fd = fs_open(O_WRONLY | O_CREAT, buf); CHECK(fd >= 0); fs_close(fd); CHECK_EQ(fs_unlink(buf), 0);
    CHECK_EQ(fs_open(O_WRONLY | O_CREAT, "bad*name"), E_NAMETOOLONG);
    CHECK_EQ(fs_open(O_WRONLY | O_CREAT, "X:/name"), E_INVAL);     /* no such drive */

    /* a file written before the clock is set gets the fixed stamp */
    clock_set(0);
    fd = fs_open(O_WRONLY | O_CREAT | O_TRUNC, "NOCLOCK.TXT"); CHECK(fd >= 0); fs_close(fd);
    CHECK_EQ(fs_stat("NOCLOCK.TXT", &e), 0); CHECK_EQ(e.mtime, 1767225600u);   /* 2026-01-01 */
    clock_set(1792618200u);

    /* directories, current directory, drive-relative paths */
    CHECK_EQ(fs_mkdir("SUB"), 0);
    CHECK_EQ(fs_mkdir("SUB"), E_EXIST);
    CHECK_EQ(fs_chdir("SUB"), 0);
    rc = fs_getcwd(buf, sizeof buf); CHECK(rc > 0); CHECK_STR(buf, "C:/SUB");
    CHECK_EQ(fs_getcwd(buf, 4), E_INVAL);
    fd = fs_open(O_WRONLY | O_CREAT, "IN.TXT"); CHECK(fd >= 0); CHECK_EQ(fs_write(fd, "sub", 3), 3); fs_close(fd);
    CHECK_EQ(fs_stat("C:/SUB/IN.TXT", &e), 0); CHECK_EQ(e.size, 3);
    CHECK_EQ(fs_stat("/SUB/IN.TXT", &e), 0);
    CHECK_EQ(fs_stat("/IN.TXT", &e), E_NOENT);                   /* absolute: the root */
    CHECK_EQ(fs_unlink("C:/SUB"), E_EXIST);                      /* not empty */
    CHECK_EQ(fs_rename("IN.TXT", "C:/OUT.TXT"), 0);              /* a move */
    CHECK_EQ(fs_stat("C:/OUT.TXT", &e), 0); CHECK_EQ(fs_stat("IN.TXT", &e), E_NOENT);
    CHECK_EQ(fs_rename("C:/OUT.TXT", "C:/NOCLOCK.TXT"), E_EXIST);
    CHECK_EQ(fs_chdir(".."), 0);
    rc = fs_getcwd(buf, sizeof buf); CHECK_STR(buf, "C:/");
    CHECK_EQ(fs_unlink("SUB"), 0);
    CHECK_EQ(fs_unlink("SUB"), E_NOENT);
    CHECK_EQ(fs_chdir("NOPE"), E_NOENT);
    CHECK_EQ(fs_unlink("OUT.TXT"), 0);
    CHECK_EQ(fs_unlink("NOCLOCK.TXT"), 0);

    /* the erase-sector cache: a 24 KB file is three data sectors plus the FAT/directory
     * sector, so a handful of program cycles, not one per 512-byte write; the content
     * reads back through the cache before the sync and from the array after it */
    flashdisk_flush();
    int w0 = flash_writes;
    fd = fs_open(O_WRONLY | O_CREAT | O_TRUNC, "BIG.BIN"); CHECK(fd >= 0);
    for (int i = 0; i < 24; i++) {
        for (int j = 0; j < 1024; j++) buf[j] = (char)(i + j);
        CHECK_EQ(fs_write(fd, buf, 1024), 1024);
    }
    CHECK(flash_writes - w0 <= 4);                               /* data lands as whole erase sectors */
    fs_close_all();
    CHECK(flash_writes - w0 <= 8);
    CHECK(!flashdisk_dirty());
    CHECK_EQ(fs_stat("BIG.BIN", &e), 0); CHECK_EQ(e.size, 24 * 1024);
    fd = fs_open(O_RDONLY, "BIG.BIN"); CHECK(fd >= 0);
    uint32_t sum = 0;
    while ((rc = fs_read(fd, buf, sizeof buf)) > 0) for (int j = 0; j < rc; j++) sum += (uint8_t)buf[j];
    CHECK_EQ(rc, 0); fs_close(fd);
    uint32_t want = 0;
    for (int i = 0; i < 24; i++) for (int j = 0; j < 1024; j++) want += (uint8_t)(i + j);
    CHECK_EQ(sum, want);
    /* unchanged sector rewrites cost nothing */
    int w1 = flash_writes;
    fd = fs_open(O_RDWR, "BIG.BIN"); CHECK(fd >= 0);
    for (int j = 0; j < 1024; j++) buf[j] = (char)(5 + j);
    CHECK_EQ(fs_lseek(fd, 5 * 1024, SEEK_SET), 5 * 1024);
    CHECK_EQ(fs_write(fd, buf, 1024), 1024);
    fs_close(fd); flashdisk_flush();
    CHECK(flash_writes - w1 <= 1);                               /* the directory stamp changed, the data did not */
    CHECK_EQ(fs_unlink("BIG.BIN"), 0);

    /* the block layer itself: a dirty sector reads back from the cache before the flush,
     * an unchanged rewrite dirties nothing, a flush lands it in the array */
    {
        static uint8_t sec[512], back[512];
        uint32_t last = FLASHDISK_SECTORS - 1;             /* in the last erase sector: free space */
        int w = flash_writes;
        memset(sec, 0x5A, sizeof sec);
        CHECK_EQ(flashdisk_write(last, sec, 1), 0);
        CHECK(flashdisk_dirty()); CHECK_EQ(flash_writes, w);
        CHECK_EQ(flashdisk_read(last, back, 1), 0); CHECK(!memcmp(back, sec, 512));
        CHECK(flashdisk_base[last * 512] == 0xFF);           /* not in the array yet */
        CHECK_EQ(flashdisk_flush(), 0); CHECK_EQ(flash_writes, w + 1); CHECK(!flashdisk_dirty());
        CHECK(flashdisk_base[last * 512] == 0x5A);
        CHECK_EQ(flashdisk_write(last, sec, 1), 0); CHECK(!flashdisk_dirty());       /* same data: nothing to do */
        CHECK_EQ(flashdisk_write(last - 16, sec, 1), 0); CHECK(flashdisk_dirty());    /* another erase sector */
        CHECK_EQ(flashdisk_write(last, sec, 1), 0); CHECK_EQ(flash_writes, w + 2);     /* moving the cache flushed it */
        CHECK_EQ(flashdisk_read(FLASHDISK_SECTORS, back, 1), -1);
        CHECK_EQ(flashdisk_write(last, sec, 2), -1);
        memset(sec, 0xFF, sizeof sec);                        /* leave the free space erased */
        flashdisk_write(last, sec, 1); flashdisk_write(last - 16, sec, 1); flashdisk_flush();
    }

    /* the volume fills up: E_NOSPC, and fs_copy leaves no partial file */
    fd = fs_open(O_WRONLY | O_CREAT | O_TRUNC, "FILL.BIN"); CHECK(fd >= 0);
    int written = 0;
    while ((rc = fs_write(fd, buf, 1024)) == 1024) written += 1024;
    CHECK_EQ(rc, E_NOSPC);
    CHECK(written > 600 * 1024 && written < 768 * 1024);
    fs_close(fd);
    CHECK_EQ(fs_unlink("FILL.BIN"), 0);
    rc = fs_getcwd(buf, sizeof buf); CHECK_STR(buf, "C:/");
}

/* ---- both drives: copy, drive switching, invalidation ---- */
static void test_two_drives(void)
{
    char buf[600];
    struct dirent e;
    int fd, rc;

    CHECK_EQ(fs_chdir("D:"), 0);
    rc = fs_getcwd(buf, sizeof buf); CHECK_STR(buf, "D:/");
    CHECK_EQ(fs_chdir("C:/"), 0);                                /* sets C:'s directory, stays on D: */
    rc = fs_getcwd(buf, sizeof buf); CHECK_STR(buf, "D:/");
    CHECK_EQ(fs_stat("HELLO.TXT", &e), 0);                       /* D:'s file, relative */
    CHECK_EQ(fs_stat("C:/HELLO.TXT", &e), E_NOENT);
    rc = fs_copy("HELLO.TXT", "C:/HCOPY.TXT", (uint8_t *)buf, sizeof buf); CHECK_EQ(rc, 13);
    CHECK_EQ(fs_stat("C:/HCOPY.TXT", &e), 0); CHECK_EQ(e.size, 13);
    CHECK_EQ(fs_stat("HELLO.TXT", &e), 0);
    struct dirent e2;
    CHECK_EQ(fs_stat("C:/HCOPY.TXT", &e2), 0); CHECK_EQ(e2.mtime, e.mtime);   /* stamp kept */
    rc = fs_copy("C:/HCOPY.TXT", "D:/HBACK.TXT", (uint8_t *)buf, sizeof buf); CHECK_EQ(rc, 13);
    fd = fs_open(O_RDONLY, "HBACK.TXT"); CHECK(fd >= 0);
    rc = fs_read(fd, buf, sizeof buf); CHECK_EQ(rc, 13); buf[13] = 0; CHECK_STR(buf, "hello, world\n"); fs_close(fd);
    CHECK_EQ(fs_copy("NOPE.TXT", "C:/X", (uint8_t *)buf, sizeof buf), E_NOENT);
    CHECK_EQ(fs_copy("/", "C:/X", (uint8_t *)buf, sizeof buf), E_NAMETOOLONG);
    CHECK_EQ(fs_unlink("HBACK.TXT"), 0);
    CHECK_EQ(fs_unlink("C:/HCOPY.TXT"), 0);

    /* a card error drops D:'s open files and directories, not C:'s or the devices */
    int fc = fs_open(O_RDONLY, "C:/README.TXT"); CHECK(fc >= 0);
    int fdd = fs_open(O_RDONLY, "HELLO.TXT"); CHECK(fdd >= 0);
    int hd = fs_opendir("D:/"); CHECK(hd >= 0);
    fs_invalidate();
    CHECK_EQ(fs_read(fdd, buf, 1), E_BADF);
    CHECK_EQ(fs_readdir(hd, &e), E_BADF);
    CHECK_EQ(fs_read(fc, buf, 1), 1);
    fs_close(fc);
    CHECK_EQ(fs_stat("HELLO.TXT", &e), 0);                       /* remounted */
}

static int find_entry(const char *name, struct dirent *out)
{
    int h = fs_opendir("/");
    if (h < 0) return h;
    int rc, found = 0;
    struct dirent e;
    while ((rc = fs_readdir(h, &e)) > 0)
        if (!strcmp(e.name, name)) { *out = e; found = 1; }
    fs_closedir(h);
    return rc < 0 ? rc : found;
}

void test_fs(const char *path, const char *cimg)
{
    char buf[600];
    struct dirent e;
    int fd, rc;

    if (!path) { printf("test_fs: no image path given, skipping\n"); return; }
    img = fopen(path, "r+b");
    if (!img) { printf("test_fs: cannot open %s, skipping\n", path); return; }
    fseek(img, 0, SEEK_END);
    img_blocks = ftell(img) / 512;
    if (img_blocks == 0) { printf("test_fs: %s is empty (mkfs.vfat/mcopy missing?), skipping\n", path); fclose(img); img = 0; return; }

    test_flash_volume(cimg);

    /* nothing works before the (lazy) mount reaches the card: emulate "no card" */
    CHECK_EQ(fs_chdir("D:"), 0);                                  /* the rest of this test lives on D: */
    fs_invalidate();
    FILE *save = img; img = 0;
    CHECK_EQ(fs_open(O_RDONLY, "README.TXT"), E_NODEV);
    CHECK_EQ(fs_getcwd(buf, sizeof buf), E_NODEV);

    /* /dev/ttya needs no card: raw bytes both ways, not seekable, survives a card error */
    test_serial_reset();
    CHECK_EQ(fs_open(O_RDONLY, "/dev/ttyz"), E_NOENT);
    CHECK_EQ(fs_open(O_RDONLY, "/dev/"), E_NOENT);
    CHECK_EQ(fs_open(O_RDWR | O_CREAT | O_TRUNC, "/dev/ttya") >= FS_FD_BASE, 1);
    int tty = fs_open(O_RDWR | O_CREAT | O_TRUNC, "/dev/ttya");
    fs_close(tty - 1);
    CHECK(tty >= FS_FD_BASE);
    CHECK_EQ(fs_write(tty, "hi\n", 3), 3);
    CHECK_STR(test_serial_out, "hi\n");                              /* no CR added */
    CHECK_EQ(fs_lseek(tty, 0, SEEK_SET), E_INVAL);
    CHECK_EQ(fs_size(tty), E_INVAL);
    test_serial_feed("abc");
    rc = fs_read(tty, buf, sizeof buf); CHECK_EQ(rc, 3); buf[3] = 0; CHECK_STR(buf, "abc");
    test_serial_feed("defgh");
    rc = fs_read(tty, buf, 2); CHECK_EQ(rc, 2); buf[2] = 0; CHECK_STR(buf, "de");   /* len caps it */
    rc = fs_read(tty, buf, 8); CHECK_EQ(rc, 3); buf[3] = 0; CHECK_STR(buf, "fgh");
    fs_invalidate();                                                 /* card error: device fd stays open */
    CHECK_EQ(fs_write(tty, "x", 1), 1);
    int ro = fs_open(O_RDONLY, "/dev/ttya");
    CHECK_EQ(fs_write(ro, "x", 1), E_BADF);
    int wo = fs_open(O_WRONLY, "/dev/ttya");
    CHECK_EQ(fs_read(wo, buf, 1), E_BADF);
    CHECK_EQ(fs_close(ro), 0); CHECK_EQ(fs_close(wo), 0);
    CHECK_EQ(fs_close(tty), 0);
    CHECK_EQ(fs_close(tty), E_BADF);
    img = save;

    /* lazy mount on first use, files put there by mcopy */
    CHECK_EQ(find_entry("README.TXT", &e), 1);
    CHECK_EQ(e.size, 33); CHECK_EQ(e.attr, 0);
    CHECK_EQ(find_entry("HELLO.TXT", &e), 1);
    CHECK_EQ(e.size, 13);
    CHECK_EQ(find_entry("NOPE.TXT", &e), 0);
    CHECK_EQ(fs_opendir("/NODIR"), E_NOENT);

    /* read */
    fd = fs_open(O_RDONLY, "HELLO.TXT");
    CHECK(fd >= FS_FD_BASE);
    CHECK_EQ(fs_size(fd), 13);
    rc = fs_read(fd, buf, sizeof buf); CHECK_EQ(rc, 13); buf[13] = 0; CHECK_STR(buf, "hello, world\n");
    CHECK_EQ(fs_read(fd, buf, sizeof buf), 0);                    /* EOF */
    CHECK_EQ(fs_lseek(fd, 7, SEEK_SET), 7);
    rc = fs_read(fd, buf, 5); CHECK_EQ(rc, 5); buf[5] = 0; CHECK_STR(buf, "world");
    CHECK_EQ(fs_lseek(fd, -5, SEEK_CUR), 7);
    CHECK_EQ(fs_lseek(fd, -1, SEEK_END), 12);
    CHECK_EQ(fs_lseek(fd, -100, SEEK_SET), E_INVAL);
    CHECK_EQ(fs_lseek(fd, 0, 9), E_INVAL);
    CHECK_EQ(fs_write(fd, "x", 1), E_BADF);                       /* read-only fd */
    CHECK_EQ(fs_close(fd), 0);
    CHECK_EQ(fs_close(fd), E_BADF);
    CHECK_EQ(fs_read(fd, buf, 1), E_BADF);
    CHECK_EQ(fs_read(99, buf, 1), E_BADF);
    CHECK_EQ(fs_read(2, buf, 1), E_BADF);                         /* console fds never reach fs.c, but be safe */
    CHECK_EQ(fs_open(O_RDONLY, "lowercase.txt"), E_NOENT);               /* a fine long name, just absent */
    fd = fs_open(O_RDONLY, "hello.txt"); CHECK(fd >= 0); fs_close(fd);
    fd = fs_open(O_RDONLY, "/hello.txt"); CHECK(fd >= 0); fs_close(fd);
    CHECK_EQ(fs_open(O_RDONLY, "nope.txt"), E_NOENT);
    CHECK_EQ(fs_open(O_RDONLY, "/"), E_NAMETOOLONG);                   /* FatFs: the root is not a file name */
    CHECK_EQ(fs_open(O_RDONLY | O_CREAT, "x.txt"), E_INVAL);     /* create needs write access */
    CHECK_EQ(fs_open(O_WRONLY, "nope.txt"), E_NOENT);            /* no O_CREAT: must exist */
    CHECK_EQ(fs_open(3, "x.txt"), E_INVAL);

    /* create, write, read back, append, truncate */
    fd = fs_open(O_WRONLY | O_CREAT | O_TRUNC, "NEW.TXT");
    CHECK(fd >= FS_FD_BASE);
    CHECK_EQ(fs_read(fd, buf, 1), E_BADF);                       /* write-only */
    for (int i = 0; i < (int)sizeof buf; i++) buf[i] = (char)('A' + i % 26);
    CHECK_EQ(fs_write(fd, buf, 600), 600);                        /* crosses a sector */
    CHECK_EQ(fs_write(fd, "tail", 4), 4);
    CHECK_EQ(fs_close(fd), 0);
    CHECK_EQ(find_entry("NEW.TXT", &e), 1); CHECK_EQ(e.size, 604);
    fd = fs_open(O_RDWR, "NEW.TXT"); CHECK(fd >= 0);
    CHECK_EQ(fs_lseek(fd, 598, SEEK_SET), 598);
    rc = fs_read(fd, buf, 10); CHECK_EQ(rc, 6); buf[6] = 0; CHECK_STR(buf, "ABtail"); /* 598 % 26 = 0 -> 'A' */
    CHECK_EQ(fs_lseek(fd, 0, SEEK_SET), 0);
    CHECK_EQ(fs_write(fd, "zz", 2), 2);
    CHECK_EQ(fs_close(fd), 0);
    fd = fs_open(O_WRONLY | O_APPEND, "NEW.TXT"); CHECK(fd >= 0);
    CHECK_EQ(fs_write(fd, "+", 1), 1);
    CHECK_EQ(fs_lseek(fd, 0, SEEK_SET), 0);
    CHECK_EQ(fs_write(fd, "!", 1), 1);                            /* O_APPEND: still goes to the end */
    CHECK_EQ(fs_close(fd), 0);
    fd = fs_open(O_RDONLY, "NEW.TXT"); CHECK(fd >= 0);
    CHECK_EQ(fs_size(fd), 606);
    rc = fs_read(fd, buf, 3); CHECK_EQ(rc, 3); buf[3] = 0; CHECK_STR(buf, "zzC");
    CHECK_EQ(fs_lseek(fd, -2, SEEK_END), 604);
    rc = fs_read(fd, buf, 3); CHECK_EQ(rc, 2); buf[2] = 0; CHECK_STR(buf, "+!");
    fs_close(fd);
    fd = fs_open(O_WRONLY | O_TRUNC, "NEW.TXT"); CHECK(fd >= 0);  /* truncate existing without O_CREAT */
    CHECK_EQ(fs_close(fd), 0);
    CHECK_EQ(find_entry("NEW.TXT", &e), 1); CHECK_EQ(e.size, 0);
    fd = fs_open(O_RDWR | O_CREAT, "NEW.TXT"); CHECK(fd >= 0);     /* open-always keeps the (empty) file */
    CHECK_EQ(fs_size(fd), 0);
    fs_close(fd);
    CHECK_EQ(fs_open(O_WRONLY | O_CREAT, "a/b.txt"), E_NOENT);    /* missing directory */
    fd = fs_open(O_WRONLY | O_CREAT, "toolongname.txt"); CHECK(fd >= 0);                 /* a long name on the card */
    CHECK_EQ(fs_write(fd, "seven b", 7), 7); fs_close(fd);
    CHECK_EQ(fs_stat("TOOLON~1.TXT", &e), 0);
    CHECK_EQ(stored_name("", "toolon~1.txt", buf), 1); CHECK_STR(buf, "toolongname.txt");
    CHECK_EQ(fs_unlink("toolongname.txt"), 0);

    /* table limits */
    int fds[FS_NFILES];
    for (int i = 0; i < FS_NFILES; i++) { fds[i] = fs_open(O_RDONLY, "HELLO.TXT"); CHECK(fds[i] >= 0); }
    CHECK_EQ(fs_open(O_RDONLY, "HELLO.TXT"), E_NFILE);
    int hs[FS_NDIRS];
    for (int i = 0; i < FS_NDIRS; i++) { hs[i] = fs_opendir("/"); CHECK(hs[i] >= 0); }
    CHECK_EQ(fs_opendir("/"), E_NFILE);
    CHECK_EQ(fs_readdir(FS_NDIRS, &e), E_BADF);
    fs_close_all();                                               /* user program exit */
    CHECK_EQ(fs_close(fds[0]), E_BADF);
    CHECK_EQ(fs_closedir(hs[0]), E_BADF);
    fd = fs_open(O_RDONLY, "HELLO.TXT"); CHECK(fd >= 0); fs_close(fd);

    /* directory listing content */
    int h = fs_opendir("/");
    int n = 0, saw_readme = 0;
    while ((rc = fs_readdir(h, &e)) > 0) { n++; if (!strcmp(e.name, "README.TXT")) saw_readme = 1; CHECK(strlen(e.name) <= 63); }
    CHECK_EQ(rc, 0); CHECK(saw_readme); CHECK(n >= 3);
    CHECK_EQ(fs_readdir(h, &e), 0);                               /* stays at end */
    fs_closedir(h);

    /* a `sd` command invalidates; everything remounts transparently */
    fs_invalidate();
    CHECK(!sd_card.ready);
    CHECK_EQ(find_entry("HELLO.TXT", &e), 1);
    CHECK(sd_card.ready);

    test_two_drives();

    CHECK_STR(fs_strerror(E_NOENT), "not found");
    fclose(img); img = 0;
}
