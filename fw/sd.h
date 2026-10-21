/* SD card on the native SD bus (spec revision 7), bit-banged through the SDIO register:
 * six open-drain lines (DAT3, CLK, CMD, DAT0..2) that read back their level. 4-bit data
 * after initialisation. 512-byte block interface. */
#ifndef SD_H
#define SD_H
#include <stdint.h>

#define SD_BLOCK_SIZE 512

enum sd_type { SD_NONE = 0, SD_V1, SD_V2, SD_V2_HC };

/* Error codes (negative); sd_strerror() names them. */
#define SD_OK            0
#define SD_ERR_NOCARD   -1      /* nothing answered during initialisation (no card) */
#define SD_ERR_TIMEOUT  -2      /* no response / no data start bit / busy forever */
#define SD_ERR_CMD      -3      /* the card status reported an error (illegal command...) */
#define SD_ERR_TOKEN    -4      /* the card rejected the written block (CRC status 101) */
#define SD_ERR_RANGE    -5      /* block beyond the card */
#define SD_ERR_NOINIT   -6      /* sd_init has not succeeded */
#define SD_ERR_VOLTAGE  -7      /* CMD8 says the card does not accept 2.7-3.6 V */
#define SD_ERR_CRC      -8      /* a response arrived with a bad CRC7 */

struct sd_info {
    enum sd_type type;
    uint32_t blocks;            /* capacity in 512-byte blocks (from the CSD) */
    uint8_t  csd[16];
    uint8_t  cid[16];
    uint8_t  ocr[4];
    uint16_t rca;               /* relative card address from CMD3 */
    uint8_t  block_addr;        /* 1: CMD17/24 take block numbers, 0: byte offsets */
    uint8_t  bus4;              /* 1: ACMD6 selected the 4-bit data bus */
    uint8_t  ready;
};

extern struct sd_info sd_card;

void sd_hw_init(void);                  /* release all six lines (SDIO = 0x3F) */
int  sd_init(void);                     /* full (re)initialisation; SD_OK or SD_ERR_* */
int  sd_read_block(uint32_t lba, void *buf);
int  sd_write_block(uint32_t lba, const void *buf);
const char *sd_strerror(int err);
const char *sd_type_name(enum sd_type t);

/* Pure helpers shared with the tests. */
uint8_t  sd_crc7(const uint8_t *p, int n);      /* returns (crc << 1) | 1, as sent on the wire */
uint32_t sd_csd_blocks(const uint8_t *csd);     /* capacity in 512-byte blocks, 0 if unknown */
void     sd_crc16x4(const uint8_t *data, int n, uint16_t crc[4]);   /* per-line CRC16 of a 4-bit block */

/* Bit-bang primitives (sd4.S on the target, C reference in the host tests); see sd4.S. */
extern uint16_t sd_slow;                        /* delay loops per half clock, 0 = full speed */
void sd4_clocks(int n);
int  sd4_cmd(const uint8_t *frame, uint8_t *resp, int nbits);
void sd4_read512(uint8_t *buf);                 /* after the start bit: 1024 nibbles, CRCs, end bit */
void sd4_write512(const uint8_t *buf, const uint8_t *crcnib);

#ifdef HOST_TEST
/* The host tests route the register accesses to the emulator's card model. */
void     sdt_write(uint16_t drive);
uint16_t sdt_read(void);
#endif

#endif
