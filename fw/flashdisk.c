/* Drive C: block layer (see flashdisk.h). FatFs's traffic pattern makes the cache pay:
 * it writes a file's data sectors in order (16 per erase sector, one program cycle) and
 * rewrites the FAT and directory sectors many times with mostly identical contents (a
 * sector that compares equal does not even dirty the cache). The cache is flushed when a
 * write lands in another erase sector, on CTRL_SYNC (f_sync / f_close), and by the
 * monitor before every prompt. Reads that hit the dirty cache come from it, so the
 * volume is always coherent. */
#include "bios.h"
#include "flash.h"
#include "flashdisk.h"

#ifdef HOST_TEST
uint8_t *flashdisk_base;
#else
#define flashdisk_base ((uint8_t *)(ROM_BASE + FLASH_VOL_OFFSET))
#endif

static uint8_t cache[FLASH_SECTOR_BYTES] __attribute__((aligned(2)));
static uint32_t cache_block;                /* erase-sector index the cache holds */
static uint8_t cache_valid, cache_dirty;
uint32_t flashdisk_programs;

int flashdisk_dirty(void) { return cache_dirty; }

int flashdisk_flush(void)
{
    if (!cache_dirty) return 0;
    cache_dirty = 0;
    flashdisk_programs++;
    int rc = flash_write_sector(flashdisk_base + cache_block * FLASH_SECTOR_BYTES, (const uint16_t *)cache);
    if (rc) { cache_valid = 0; return -1; }
    return 0;
}

int flashdisk_read(uint32_t sector, uint8_t *buf, unsigned count)
{
    if (sector + count > FLASHDISK_SECTORS || sector + count < sector) return -1;
    while (count--) {
        const uint8_t *src = flashdisk_base + sector * 512;
        if (cache_valid && cache_dirty && sector / FLASHDISK_BLOCK == cache_block)
            src = cache + (sector % FLASHDISK_BLOCK) * 512;
        memcpy(buf, src, 512);
        buf += 512;
        sector++;
    }
    return 0;
}

int flashdisk_write(uint32_t sector, const uint8_t *buf, unsigned count)
{
    if (sector + count > FLASHDISK_SECTORS || sector + count < sector) return -1;
    while (count--) {
        uint32_t blk = sector / FLASHDISK_BLOCK;
        if (!cache_valid || blk != cache_block) {
            if (flashdisk_flush()) return -1;
            memcpy(cache, flashdisk_base + blk * FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES);
            cache_block = blk;
            cache_valid = 1;
        }
        uint8_t *dst = cache + (sector % FLASHDISK_BLOCK) * 512;
        if (memcmp(dst, buf, 512)) {
            memcpy(dst, buf, 512);
            cache_dirty = 1;
        }
        buf += 512;
        sector++;
    }
    return 0;
}
