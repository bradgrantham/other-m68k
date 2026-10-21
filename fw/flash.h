/* In-system reprogramming of the two SST39SF040 ROM chips (monitor `flashid`, `flash`). */
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
    uint32_t fail_addr;
};

/* Read the software IDs (word per lane: even chip in the high byte, odd chip in the low).
 * Returns 1 when both chips answer FLASH_MFR_ID/FLASH_DEV_ID. */
int flash_read_id(uint16_t *mfr, uint16_t *dev);

/* Program the ROM image file `path` (exactly ROM_IMAGE_SIZE bytes, zero word sum as
 * fw/mkrom.py writes it) into the flash after a y/N prompt.  Clobbers user RAM (the
 * image is staged at USER_BASE).  Does not return when the update starts: the new ROM
 * boots on success, the CPU stops with a message on the serial port on failure. */
void flash_update(const char *path);

#endif
