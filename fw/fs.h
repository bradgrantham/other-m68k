/* File table on top of FatFs: fds 3.. are files or devices, directory handles 0.. are
 * separate. Paths starting with /dev/ open a device (spec serial.devices): /dev/ttya is
 * DUART channel A, a raw byte stream that needs no card. Every function returns
 * 0/count/fd or a negative ABI error (E_*). No pointer checks here; syscall.c validates
 * user pointers before calling in. */
#ifndef FS_H
#define FS_H
#include <stdint.h>
#include "syscalls.h"

#define FS_NFILES  8
#define FS_NDIRS   4
#define FS_FD_BASE 3
#define FS_PATH_MAX 128

int  fs_mount(void);                    /* mount if not mounted; 0 or E_* */
void fs_invalidate(void);               /* forget the mount and the card; next use re-inits */
int  fs_open(int flags, const char *path);
int  fs_close(int fd);
int  fs_read(int fd, void *buf, unsigned len);
int  fs_write(int fd, const void *buf, unsigned len);
int  fs_lseek(int fd, int offset, int whence);
int  fs_size(int fd);                   /* file size, or E_* */
int  fs_opendir(const char *path);
int  fs_readdir(int handle, struct dirent *ent);
int  fs_closedir(int handle);
void fs_close_all(void);                /* user program ended: release every slot */
const char *fs_strerror(int err);       /* E_* -> text */

#endif
