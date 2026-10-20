/* SD card in SPI mode, bit-banged through the SDIO register. */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

/* Attach an image (NULL = no card).  Returns 0 or -1. */
int  sd_init(const char *image_path, int trace);
void sd_shutdown(void);
void sd_reset(void);

/* New pin levels from the SDIO register (CS active low, SPI mode 0). */
void sd_pins(int cs, int clk, int mosi);

/* Current MISO level (1 when no card or CS high). */
int  sd_miso(void);

#endif
