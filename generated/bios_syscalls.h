/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
#ifndef BIOS_SYSCALLS_H
#define BIOS_SYSCALLS_H
#include "syscalls.h"

void bios_exit(int code);
int bios_putchar(int c);
int bios_getchar(void);
int bios_pollchar(void);
int bios_puts(const char * s);
int bios_serial_putc(int c);
int bios_serial_getc(void);
uint32_t bios_ticks(void);
void bios_wait_vblank(void);
int bios_video_info(struct video_info * info);
int bios_video_set_page(int page);
int bios_open(int flags, const char * path);
int bios_close(int fd);
int bios_read(int fd, unsigned len, void * buf);
int bios_write(int fd, unsigned len, const void * buf);
int bios_lseek(int fd, int offset, int whence);
int bios_opendir(const char * path);
int bios_readdir(int handle, struct dirent * ent);
int bios_closedir(int handle);
int bios_sd_read_block(uint32_t lba, void * buf);
int bios_sd_write_block(uint32_t lba, const void * buf);
void * bios_set_irq_handler(int level, void * handler);
int bios_mem_info(struct mem_info * info);
int bios_con_set_cursor(int x, int y);
int bios_con_clear(void);
int bios_con_info(struct con_info * info);
int bios_exec(const char * path, const char * args);
int bios_key_event(void);
void bios_sleep_ms(uint32_t ms);
int bios_set_audio_cb(void * cb);
uint8_t * bios_audio_start(unsigned bufsize);
uint8_t * bios_audio_next(const uint8_t * filled, int block);
int bios_audio_last(const uint8_t * filled, int block);
int bios_unlink(const char * path);
int bios_rename(const char * old, const char * new);
int bios_mkdir(const char * path);
int bios_chdir(const char * path);
int bios_getcwd(unsigned len, char * buf);
uint32_t bios_time(void);
int bios_set_time(uint32_t secs);

#endif
