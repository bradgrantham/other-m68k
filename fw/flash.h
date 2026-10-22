/* In-system reprogramming of the two SST39SF040 ROM chips: the monitor's `flashid` and
 * `flash` commands, and the sector write the C: volume's driver (flashdisk.c) uses. */
#ifndef FLASH_H
#define FLASH_H
#include <stdint.h>
#include "flash_layout.h"

struct flash_op {
    uint32_t cmd1, cmd2;
    uint32_t dst;
    uint32_t src;
    uint32_t nwords;
    uint32_t erase_loops, prog_loops;
    uint16_t op;
    uint16_t id_mfr, id_dev, sector_words;
    uint32_t vblanks;
};

/* Read the software IDs (word per lane: even chip in the high byte, odd chip in the low).
 * Returns 1 when both chips answer FLASH_MFR_ID/FLASH_DEV_ID. */
int flash_read_id(uint16_t *mfr, uint16_t *dev);

/* Program the ROM image file `path` (exactly ROM_IMAGE_SIZE bytes, zero word sum as
 * fw/mkrom.py writes it) into the flash after a y/N prompt.  Clobbers user RAM (the
 * image is staged at USER_BASE).  Does not return when the update starts: the new ROM
 * boots on success, the CPU stops with a message on the serial port on failure.  The C:
 * volume lives above ROM_IMAGE_SIZE and is not touched. */
void flash_update(const char *path);

/* Erase the FLASH_SECTOR_BYTES sector at `dst` (a sector-aligned address in the ROM
 * window, as a pointer so the host tests can stand in a RAM array) and program it with the
 * FLASH_SECTOR_BYTES/2 words at `src`.  Interrupts are
 * masked for the duration (about 100 ms); the frames the VBLANK interrupt missed are
 * added to `ticks` and the clock.  0 ok, 1 erase timeout, 2 program timeout, 3 verify
 * mismatch. */
int flash_write_sector(void *dst, const uint16_t *src);

#endif
