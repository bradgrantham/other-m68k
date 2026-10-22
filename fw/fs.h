/* File table on top of FatFs: fds 3.. are files or devices, directory handles 0.. are
 * separate. Two volumes (spec "filesystem"): "C:" is the flash volume (physical drive
 * FS_PDRV_FLASH), "D:" the SD card; paths may carry a drive letter and are otherwise
 * relative to the current drive's current directory. Paths starting with /dev/ open a
 * device (spec serial.devices): /dev/ttya is DUART channel A, a raw byte stream that
 * needs no card. Every function returns 0/count/fd or a negative ABI error (E_*). No
 * pointer checks here; syscall.c validates user pointers before calling in. */
#ifndef FS_H
#define FS_H
#include <stdint.h>
#include "syscalls.h"

#define FS_NFILES  8
#define FS_NDIRS   4
#define FS_FD_BASE 3
#define FS_PATH_MAX 128

#define FS_PDRV_FLASH 0                 /* volume "C:" */
#define FS_PDRV_SD    1                 /* volume "D:" */

void fs_init(void);                     /* register both volumes (mounted on first use) */
void fs_invalidate(void);               /* forget the card's mount; next use re-inits it */
int  fs_sync(void);                     /* write back the flash volume's dirty cache; 0 or E_* */
int  fs_open(int flags, const char *path);
int  fs_close(int fd);
int  fs_read(int fd, void *buf, unsigned len);
int  fs_write(int fd, const void *buf, unsigned len);
int  fs_lseek(int fd, int offset, int whence);
int  fs_size(int fd);                   /* file size, or E_* */
int  fs_opendir(const char *path);
int  fs_readdir(int handle, struct dirent *ent);
int  fs_closedir(int handle);
int  fs_stat(const char *path, struct dirent *ent);     /* 0 or E_*; ent may be NULL */
int  fs_unlink(const char *path);       /* file or empty directory */
int  fs_rename(const char *oldp, const char *newp);
int  fs_mkdir(const char *path);
int  fs_chdir(const char *path);        /* "X:" alone switches the current drive */
int  fs_getcwd(char *buf, unsigned len);        /* "X:/dir"; length or E_* */
/* Copy one file; buf (buflen bytes, >= 512) is the transfer buffer. The destination gets
 * the source's modification time. Returns the number of bytes copied or E_*. */
int  fs_copy(const char *src, const char *dst, uint8_t *buf, unsigned buflen);
/* Recreate the flash volume (drive "C:" only) as an empty FAT12 volume; buf >= 512 bytes. */
int  fs_mkfs(const char *drive, uint8_t *buf, unsigned buflen);
void fs_close_all(void);                /* user program ended: release every slot */
const char *fs_strerror(int err);       /* E_* -> text */

#endif
