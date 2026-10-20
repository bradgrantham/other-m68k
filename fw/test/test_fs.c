/* FatFs + the fd table (fs.c) against a FAT32 image file through a host diskio. */
#include "test.h"
#include "fs.h"
#include "sd.h"
#include "ff.h"
#include "diskio.h"

static FILE *img;
static long img_blocks;

DSTATUS disk_status(BYTE pdrv) { return (pdrv == 0 && img && sd_card.ready) ? 0 : STA_NOINIT; }
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0 || !img) return STA_NOINIT;
    sd_card.ready = 1; sd_card.blocks = (uint32_t)img_blocks;   /* stands in for sd_init */
    return 0;
}
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !img) return RES_NOTRDY;
    if (fseek(img, (long)sector * 512, SEEK_SET)) return RES_ERROR;
    return fread(buff, 512, count, img) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !img) return RES_NOTRDY;
    if (fseek(img, (long)sector * 512, SEEK_SET)) return RES_ERROR;
    return fwrite(buff, 512, count, img) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC: fflush(img); return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = (LBA_t)img_blocks; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
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

void test_fs(const char *path)
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

    /* nothing works before the (lazy) mount reaches the card: emulate "no card" */
    fs_invalidate();
    FILE *save = img; img = 0;
    CHECK_EQ(fs_open(O_RDONLY, "README.TXT"), E_IO);
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
    CHECK_EQ(fs_open(O_RDONLY, "lowercase.txt"), E_INVAL);        /* 9-character base name: not 8.3 */
    fd = fs_open(O_RDONLY, "hello.txt"); CHECK(fd >= 0); fs_close(fd);
    fd = fs_open(O_RDONLY, "/hello.txt"); CHECK(fd >= 0); fs_close(fd);
    CHECK_EQ(fs_open(O_RDONLY, "nope.txt"), E_NOENT);
    CHECK_EQ(fs_open(O_RDONLY, "/"), E_INVAL);                   /* FatFs: the root is not a file name */
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
    CHECK_EQ(fs_open(O_WRONLY | O_CREAT, "toolongname.txt"), E_INVAL);   /* not 8.3 without LFN */

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
    while ((rc = fs_readdir(h, &e)) > 0) { n++; if (!strcmp(e.name, "README.TXT")) saw_readme = 1; CHECK(strlen(e.name) <= 12); }
    CHECK_EQ(rc, 0); CHECK(saw_readme); CHECK(n >= 3);
    CHECK_EQ(fs_readdir(h, &e), 0);                               /* stays at end */
    fs_closedir(h);

    /* a `sd` command invalidates; everything remounts transparently */
    fs_invalidate();
    CHECK(!sd_card.ready);
    CHECK_EQ(find_entry("HELLO.TXT", &e), 1);
    CHECK(sd_card.ready);

    CHECK_STR(fs_strerror(E_NOENT), "not found");
    fclose(img); img = 0;
}
