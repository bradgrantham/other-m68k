/* SD card in SPI mode, bit-banged through the write-only SDIO register (shadow in
 * sdio_shadow) with MISO read from INTSTAT.SDMISO. 512-byte block interface. */
#ifndef SD_H
#define SD_H
#include <stdint.h>

#define SD_BLOCK_SIZE 512

enum sd_type { SD_NONE = 0, SD_V1, SD_V2, SD_V2_HC };

/* Error codes (negative); sd_strerror() names them. */
#define SD_OK            0
#define SD_ERR_NOCARD   -1      /* no response to CMD0 (no card or no MISO) */
#define SD_ERR_TIMEOUT  -2      /* no response / no data token / busy forever */
#define SD_ERR_CMD      -3      /* R1 reported an error (illegal command, CRC, address...) */
#define SD_ERR_TOKEN    -4      /* data error token or bad data response */
#define SD_ERR_RANGE    -5      /* block beyond the card */
#define SD_ERR_NOINIT   -6      /* sd_init has not succeeded */
#define SD_ERR_VOLTAGE  -7      /* CMD8 says the card does not accept 2.7-3.6 V */

struct sd_info {
    enum sd_type type;
    uint32_t blocks;            /* capacity in 512-byte blocks (from the CSD) */
    uint8_t  csd[16];
    uint8_t  cid[16];
    uint8_t  ocr[4];
    uint8_t  block_addr;        /* 1: CMD17/24 take block numbers, 0: byte offsets */
    uint8_t  ready;
};

extern uint16_t sdio_shadow;            /* image of the write-only IO_SDIO */
extern struct sd_info sd_card;

void sd_hw_init(void);                  /* SDIO to its idle value (CS high, CLK low) */
int  sd_init(void);                     /* full (re)initialisation; SD_OK or SD_ERR_* */
int  sd_read_block(uint32_t lba, void *buf);
int  sd_write_block(uint32_t lba, const void *buf);
const char *sd_strerror(int err);
const char *sd_type_name(enum sd_type t);

/* Low-level SPI hooks (sd.c on the target, a fake card in the host tests). */
uint8_t sdhw_xfer(uint8_t out);         /* exchange one byte */
void sdhw_read(uint8_t *buf, int n);    /* receive n bytes sending 0xFF (sdspi.S fast path) */
void sdhw_select(int on);               /* CS low when on */
void sdhw_slow(int on);                 /* <= 400 kHz clock during card identification */

/* Pure helpers shared with the tests. */
uint8_t sd_crc7(const uint8_t *p, int n);   /* returns (crc << 1) | 1, as sent on the wire */
uint32_t sd_csd_blocks(const uint8_t *csd); /* capacity in 512-byte blocks, 0 if unknown */

#endif
