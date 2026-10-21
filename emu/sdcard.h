/* SD card in native SD bus mode (spec revision 7), bit-banged through the SDIO register.
 *
 * The six card lines live in the SDIO bit positions of hw.h: DAT3 (bit 0), CLK (1), CMD (2),
 * DAT0 (3), DAT1 (4), DAT2 (5).  Every line is open drain with a pull-up: the host writes 1 to
 * release a line and 0 to drive it low; the card does the same on CMD and DAT0..3; what both
 * sides see is the AND of the two drives. */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

/* Attach an image (NULL = no card).  sdsc != 0 emulates a byte-addressed SDSC card (CSD v1,
 * image size a multiple of 256 KB) instead of the default block-addressed SDHC (CSD v2).
 * Returns 0 or -1. */
int  sd_init(const char *image_path, int trace, int sdsc);
void sd_shutdown(void);
void sd_reset(void);

/* The host's drive bits changed (SDIO write): 1 = released, 0 = driven low, in the SDIO
 * layout.  CLK edges clock the card: it samples CMD/DAT on the rising edge and updates its
 * own outputs after the falling edge. */
void sd_bus(unsigned host_drive);

/* The card's drive bits in the SDIO layout (1 = released).  The level on each line is
 * host_drive & sd_lines().  0x3F when no image is attached. */
unsigned sd_lines(void);

#endif
