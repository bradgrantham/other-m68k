/* FatFs physical drive 0: the FAT12 volume in the flash, drive C: (spec flash.volume_desc).
 * 512-byte sectors read straight from the ROM window; writes go through one erase-sector
 * (8 KB) cache that is written back by flash_write_sector when it has to move or on sync. */
#ifndef FLASHDISK_H
#define FLASHDISK_H
#include <stdint.h>

#define FLASHDISK_SECTORS   (FLASH_VOL_SIZE / 512)
#define FLASHDISK_BLOCK     (FLASH_SECTOR_BYTES / 512)      /* sectors per erase sector */

int flashdisk_read(uint32_t sector, uint8_t *buf, unsigned count);     /* 0 or -1 */
int flashdisk_write(uint32_t sector, const uint8_t *buf, unsigned count);
int flashdisk_flush(void);                  /* write the dirty cache back; 0 or -1 */
int flashdisk_dirty(void);
extern uint32_t flashdisk_programs;         /* erase+program cycles since boot */
#ifdef HOST_TEST
extern uint8_t *flashdisk_base;             /* the test's stand-in for the flash array */
#endif

#endif
