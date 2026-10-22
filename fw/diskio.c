/* FatFs media layer. Physical drive 0 (volume "C:") is the flash volume (flashdisk.c),
 * drive 1 ("D:") the SD card (sd.c). FatFs calls disk_status on every volume access and
 * disk_initialize when it reports STA_NOINIT, so clearing sd_card.ready (fs_invalidate)
 * is enough to force a card re-init and remount. */
#include "bios.h"
#include "sd.h"
#include "fs.h"
#include "flashdisk.h"
#include "ff.h"
#include "diskio.h"

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv == FS_PDRV_FLASH) return 0;
    if (pdrv == FS_PDRV_SD) return sd_card.ready ? 0 : STA_NOINIT;
    return STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv == FS_PDRV_FLASH) return 0;
    if (pdrv != FS_PDRV_SD) return STA_NOINIT;
    if (!sd_card.ready && sd_init() != SD_OK) return STA_NOINIT;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == FS_PDRV_FLASH) return flashdisk_read((uint32_t)sector, buff, count) ? RES_PARERR : RES_OK;
    if (pdrv != FS_PDRV_SD) return RES_PARERR;
    while (count--) {
        int rc = sd_read_block((uint32_t)sector, buff);
        if (rc == SD_ERR_RANGE) return RES_PARERR;
        if (rc == SD_ERR_NOINIT) return RES_NOTRDY;
        if (rc) return RES_ERROR;
        sector++;
        buff += SD_BLOCK_SIZE;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == FS_PDRV_FLASH) return flashdisk_write((uint32_t)sector, buff, count) ? RES_ERROR : RES_OK;
    if (pdrv != FS_PDRV_SD) return RES_PARERR;
    while (count--) {
        int rc = sd_write_block((uint32_t)sector, buff);
        if (rc == SD_ERR_RANGE) return RES_PARERR;
        if (rc == SD_ERR_NOINIT) return RES_NOTRDY;
        if (rc) return RES_ERROR;
        sector++;
        buff += SD_BLOCK_SIZE;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv == FS_PDRV_FLASH) {
        switch (cmd) {
        case CTRL_SYNC: return flashdisk_flush() ? RES_ERROR : RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)buff = FLASHDISK_SECTORS; return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD *)buff = FLASHDISK_BLOCK; return RES_OK;   /* erase unit, for f_mkfs */
        default: return RES_PARERR;
        }
    }
    if (pdrv != FS_PDRV_SD) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;                  /* writes complete before sd_write_block returns */
    case GET_SECTOR_COUNT: *(LBA_t *)buff = sd_card.blocks; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
