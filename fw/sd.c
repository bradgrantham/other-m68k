/* SD card driver: native SD bus, bit-banged on the SDIO register (spec revision 7).
 *
 * Lines (SDIO bits): DAT3 0, CLK 1, CMD 2, DAT0 3, DAT1 4, DAT2 5. Every bit is an
 * open-drain driver: 0 drives the line low, 1 releases it to the 10 k pull-up; a read
 * returns the level of each line (host AND card). One clock = write the drive word with CLK
 * low (the card updates CMD/DAT after the falling edge), read the levels, write it with CLK
 * high (the card samples CMD/DAT on the rising edge). Each register access is one bus
 * cycle (8 clocks). Identification runs slower (sd_slow) to stay under the 400 kHz the spec
 * requires until the card has an RCA.
 *
 * Protocol (SD Physical Layer Simplified Spec, chapter 4): 48-bit command frames on CMD with
 * CRC7; R1/R3/R6/R7 responses are 48 bits, R2 (CID/CSD) 136 bits; after init ACMD6 selects
 * the 4-bit data bus, so a 512-byte block is a start bit, 1024 nibbles on DAT3..0 (DAT3 =
 * MSB, high nibble first), 16 CRC16 clocks (one CRC per line) and an end bit. Reads ignore the
 * CRCs (the fast path is sd4_read512 in sd4.S); writes compute the four CRC16s
 * (sd_crc16x4), then read the CRC status token on DAT0 and wait out the busy signal.
 *
 * Only the monitor and the TRAP #15 handlers call this; no ISR touches SDIO and the card
 * tolerates any pause of its clock, so interrupts stay enabled throughout. */
#include "bios.h"
#include "sd.h"

struct sd_info sd_card;

/* ---- pin layer --------------------------------------------------------------------- */
uint16_t sd_slow;           /* sd4.S: delay loops per half clock, 0 = full speed */
#ifdef HOST_TEST
#define SDIO_W(v)   sdt_write((uint16_t)(v))
#define SDIO_R()    sdt_read()
#else
#define SDIO_W(v)   (IO_SDIO = (uint16_t)(v))
#define SDIO_R()    (IO_SDIO)
#endif
#define SD_SLOW_LOOPS 20    /* x 10 clocks per half clock: ~25 kHz during identification */

#define SD_LO   (IO_SDIO_RESET & ~SDIO_CLK)         /* 0x3D: everything released, CLK low */
#define SD_HI   IO_SDIO_RESET                       /* 0x3F */

/* limits in clocks (one clock = one sd_clk call) */
#define SD_NCR_MAX        128       /* response start after the command (spec: 64) */
#define SD_INIT_POLLS     2000      /* ACMD41 iterations (~1 s) */
#define SD_READ_WAIT      50000     /* data start bit after R1 (~200 ms; spec: 100 ms) */
#define SD_TOKEN_WAIT     1000      /* CRC status token after the written block */
#define SD_BUSY_WAIT      100000    /* programming busy (~400 ms; spec: 250 ms) */

/* One full-speed clock with the given drive word (CLK low); returns the levels read with
 * CLK low. Used for the waits (start bits, CRC token, busy); the bulk work is in sd4.S. */
static inline unsigned sd_clk(unsigned lo)
{
    SDIO_W(lo);
    unsigned v = SDIO_R();
    SDIO_W(lo | SDIO_CLK);
    return v;
}

#define sd_clocks(n) sd4_clocks(n)

#ifdef HOST_TEST
/* C reference versions of the sd4.S primitives for the host tests. */
void sd4_clocks(int n)
{
    while (n--) sd_clk(SD_LO);
}

int sd4_cmd(const uint8_t *frame, uint8_t *resp, int nbits)
{
    for (int i = 0; i < 6; i++)
        for (unsigned m = 0x80; m; m >>= 1)
            sd_clk((frame[i] & m) ? SD_LO : (SD_LO & ~SDIO_CMD));
    if (!nbits) return 0;
    int i;
    for (i = 0; i < 128; i++)
        if (!(sd_clk(SD_LO) & SDIO_CMD)) break;
    if (i == 128) return -1;
    unsigned acc = 0;                           /* start bit (0) is the first bit */
    int n = 1, k = 0;
    for (int b = 1; b < nbits; b++) {
        acc = (acc << 1) | ((sd_clk(SD_LO) >> SDIO_CMD_BIT) & 1);
        if (++n == 8) { resp[k++] = (uint8_t)acc; acc = 0; n = 0; }
    }
    return 0;
}

#define NIB(v) ((((v) >> SDIO_DAT3_BIT) & 1) << 3 | (((v) >> SDIO_DAT2_BIT) & 1) << 2 | \
                (((v) >> SDIO_DAT1_BIT) & 1) << 1 | (((v) >> SDIO_DAT0_BIT) & 1))

void sd4_read512(uint8_t *buf)
{
    for (int i = 0; i < SD_BLOCK_SIZE; i++) {
        unsigned hi = sd_clk(SD_LO), lo = sd_clk(SD_LO);
        buf[i] = (uint8_t)(NIB(hi) << 4 | NIB(lo));
    }
    sd_clocks(17);                              /* CRC16s and the end bit */
}

/* Drive word (CLK low) for a nibble on DAT3..0: a 0 bit drives its line low. */
static const uint8_t sd_nib_drive[16] = {
    0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C,
    0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D,
};

void sd4_write512(const uint8_t *buf, const uint8_t *crcnib)
{
    sd_clk(sd_nib_drive[0]);                    /* start bit on all four lines */
    for (int i = 0; i < SD_BLOCK_SIZE; i++) {
        sd_clk(sd_nib_drive[buf[i] >> 4]);
        sd_clk(sd_nib_drive[buf[i] & 15]);
    }
    for (int i = 0; i < 16; i++) sd_clk(sd_nib_drive[crcnib[i]]);
    sd_clk(SD_LO);                              /* end bit */
}
#endif

void sd_hw_init(void)
{
    SDIO_W(SD_HI);
}

/* ---- CRCs ---------------------------------------------------------------------- */

/* CRC7 (x^7 + x^3 + 1) over n bytes, returned shifted left with the end bit set, as it
 * appears in the last byte of a command frame, an R1/R6/R7 response, the CID or the CSD. */
uint8_t sd_crc7(const uint8_t *p, int n)
{
    uint8_t crc = 0;
    for (int i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x12 : (crc << 1));
    }
    return (uint8_t)(crc | 1);          /* the loop leaves it shifted left by one bit */
}

#ifdef HOST_TEST
/* C reference of the sd4.S routine for the host tests. */
/* CRC16-CCITT (x^16 + x^12 + x^5 + 1, init 0) of each of the four DAT lines of a 4-bit block.
 * Line L carries bit L of every nibble, high nibble first. TN[n] spreads the four bits of a
 * nibble into four byte lanes (lane L = bit L); four data bytes (eight nibbles) then make one
 * byte of each line's stream in a long, and each lane byte goes through the nibble table. */
static const uint32_t TN[16] = {
    0x00000000, 0x00000001, 0x00000100, 0x00000101, 0x00010000, 0x00010001, 0x00010100, 0x00010101,
    0x01000000, 0x01000001, 0x01000100, 0x01000101, 0x01010000, 0x01010001, 0x01010100, 0x01010101,
};
static const uint16_t T4[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
};

static inline uint16_t crc16_byte(uint16_t crc, uint8_t b)
{
    crc = (uint16_t)((crc << 4) ^ T4[((crc >> 12) ^ (b >> 4)) & 0xF]);
    crc = (uint16_t)((crc << 4) ^ T4[((crc >> 12) ^ b) & 0xF]);
    return crc;
}

void sd_crc16x4(const uint8_t *data, int n, uint16_t crc[4])
{
    crc[0] = crc[1] = crc[2] = crc[3] = 0;
    for (int i = 0; i + 4 <= n; i += 4) {
        uint32_t lanes = ((TN[data[i] >> 4] << 1 | TN[data[i] & 15]) << 6)
                       | ((TN[data[i + 1] >> 4] << 1 | TN[data[i + 1] & 15]) << 4)
                       | ((TN[data[i + 2] >> 4] << 1 | TN[data[i + 2] & 15]) << 2)
                       | (TN[data[i + 3] >> 4] << 1 | TN[data[i + 3] & 15]);
        crc[0] = crc16_byte(crc[0], (uint8_t)lanes);
        crc[1] = crc16_byte(crc[1], (uint8_t)(lanes >> 8));
        crc[2] = crc16_byte(crc[2], (uint8_t)(lanes >> 16));
        crc[3] = crc16_byte(crc[3], (uint8_t)(lanes >> 24));
    }
}
#endif

/* ---- capacity from the CSD (spec 5.3.2 / 5.3.3) --------------------------------- */

uint32_t sd_csd_blocks(const uint8_t *csd)
{
    if ((csd[0] >> 6) == 1) {           /* CSD version 2.0: (C_SIZE + 1) * 512 KB */
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1) * 1024;
    }
    if ((csd[0] >> 6) == 0) {           /* version 1.0 */
        uint32_t read_bl_len = csd[5] & 0x0F;
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        uint32_t c_size_mult = (uint32_t)((csd[9] & 0x03) << 1) | (csd[10] >> 7);
        uint32_t blocks = (c_size + 1) << (c_size_mult + 2);    /* in units of 2^READ_BL_LEN */
        if (read_bl_len < 9) return blocks >> (9 - read_bl_len);
        return blocks << (read_bl_len - 9);
    }
    return 0;
}

/* ---- command layer ---------------------------------------------------------------- */

/* Send a command frame (start 0, transmission 1, index, argument, CRC7, end 1) and read a
 * response of nbits (48 or 136; 0 = none) into resp, MSB first with the start bit in bit 7
 * of resp[0]. SD_ERR_TIMEOUT if no start bit appears within 128 clocks (spec Ncr <= 64). */
static int sd_xfer(uint8_t cmd, uint32_t arg, uint8_t *resp, int nbits)
{
    uint8_t f[6];
    f[0] = (uint8_t)(0x40 | cmd);
    f[1] = (uint8_t)(arg >> 24);
    f[2] = (uint8_t)(arg >> 16);
    f[3] = (uint8_t)(arg >> 8);
    f[4] = (uint8_t)arg;
    f[5] = sd_crc7(f, 5);
    return sd4_cmd(f, resp, nbits) ? SD_ERR_TIMEOUT : SD_OK;
}

/* Nrc: 8 clocks after a response before the next command. Not after a read command: the
 * data block can start as soon as the card is ready (Nac >= 2) and the start-bit wait in
 * sd_read_block must see it. */
static void sd_after(uint8_t cmd)
{
    if (cmd != 17 && cmd != 18) sd_clocks(8);
}

/* Command with a 48-bit response. kind: 1 = R1/R7 (CRC7 and index checked), 6 = R6 (index
 * 3), 3 = R3 (no CRC). *payload gets the 32 bits after the index. */
static int sd_cmd48(uint8_t cmd, uint32_t arg, int kind, uint32_t *payload)
{
    uint8_t r[6];
    int rc = sd_xfer(cmd, arg, r, 48);
    if (rc) return rc;
    sd_after(cmd);
    if (kind != 3) {
        if (sd_crc7(r, 5) != r[5]) return SD_ERR_CRC;
        if ((r[0] & 0x3F) != (kind == 6 ? 3 : cmd)) return SD_ERR_CRC;
    }
    *payload = ((uint32_t)r[1] << 24) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 8) | r[4];
    return SD_OK;
}

/* Command with an R2 response: the 16-byte register (CID or CSD, its own CRC7 last). */
static int sd_cmd_r2(uint8_t cmd, uint32_t arg, uint8_t *reg16)
{
    uint8_t r[17];
    int rc = sd_xfer(cmd, arg, r, 136);
    if (rc) return rc;
    sd_after(cmd);
    memcpy(reg16, r + 1, 16);
    if (sd_crc7(reg16, 15) != reg16[15]) return SD_ERR_CRC;
    return SD_OK;
}

/* Card status (R1 payload) error bits: OUT_OF_RANGE 31, ADDRESS_ERROR 30, BLOCK_LEN_ERROR
 * 29, ERASE_SEQ 28, ERASE_PARAM 27, WP_VIOLATION 26, LOCK_UNLOCK_FAILED 24, COM_CRC_ERROR 23,
 * ILLEGAL_COMMAND 22, CARD_ECC_FAILED 21, CC_ERROR 20, ERROR 19, CSD_OVERWRITE 16,
 * WP_ERASE_SKIP 15, AKE_SEQ_ERROR 3. */
#define SD_STATUS_ERRORS 0xFDF98008u

static int sd_status_error(uint32_t st)
{
    if (!(st & SD_STATUS_ERRORS)) return SD_OK;
    if (st & 0xC0000000u) return SD_ERR_RANGE;
    if (st & (1u << 23)) return SD_ERR_CRC;
    return SD_ERR_CMD;
}

/* R1 command: send, check, return SD_OK or an error; *st gets the status if wanted. */
static int sd_cmd_r1(uint8_t cmd, uint32_t arg, uint32_t *st)
{
    uint32_t status;
    int rc = sd_cmd48(cmd, arg, 1, &status);
    if (rc) return rc;
    if (st) *st = status;
    return sd_status_error(status);
}

/* CMD55 then the application command (R1). */
static int sd_acmd_r1(uint8_t cmd, uint32_t arg, uint32_t *st)
{
    uint32_t status;
    int rc = sd_cmd_r1(55, (uint32_t)sd_card.rca << 16, &status);
    if (rc) return rc;
    if (!(status & (1u << 5))) return SD_ERR_CMD;        /* APP_CMD not accepted */
    return sd_cmd_r1(cmd, arg, st);
}

/* ---- initialisation --------------------------------------------------------------- */

int sd_init(void)
{
    int rc;
    uint32_t p;

    memset(&sd_card, 0, sizeof sd_card);
    sd_hw_init();
    sd_slow = SD_SLOW_LOOPS;
    sd_clocks(80);                              /* >= 74 clocks with everything released */

    /* CMD0: reset; no response. */
    sd_xfer(0, 0, 0, 0);
    sd_clocks(8);

    /* CMD8: voltage check. v2 cards echo the pattern in R7; v1 cards do not answer. */
    rc = sd_cmd48(8, 0x000001AA, 1, &p);
    if (rc == SD_OK) {
        if ((p & 0xFFF) != 0x1AA) { rc = SD_ERR_VOLTAGE; goto fail; }
        sd_card.type = SD_V2;
    } else if (rc == SD_ERR_TIMEOUT) {
        sd_card.type = SD_V1;
    } else goto fail;

    /* ACMD41 with the voltage window (and HCS for v2) until the card leaves busy. A card
     * that never answers CMD55 is no card at all. */
    {
        uint32_t arg = (sd_card.type == SD_V2 ? 0x40000000u : 0) | 0x00FF8000u;
        int polls = SD_INIT_POLLS;
        for (;;) {
            uint32_t st;
            rc = sd_cmd_r1(55, 0, &st);
            if (rc == SD_ERR_TIMEOUT) { rc = SD_ERR_NOCARD; goto fail; }
            if (rc) goto fail;
            rc = sd_cmd48(41, arg, 3, &p);      /* R3: OCR */
            if (rc) goto fail;
            if (p & 0x80000000u) break;
            if (--polls == 0) { rc = SD_ERR_TIMEOUT; goto fail; }
        }
        sd_card.ocr[0] = (uint8_t)(p >> 24); sd_card.ocr[1] = (uint8_t)(p >> 16);
        sd_card.ocr[2] = (uint8_t)(p >> 8);  sd_card.ocr[3] = (uint8_t)p;
        if (p & 0x40000000u) { sd_card.type = SD_V2_HC; sd_card.block_addr = 1; }
    }

    /* CMD2: CID (R2). CMD3: RCA (R6). Identification ends here: full speed from now on. */
    rc = sd_cmd_r2(2, 0, sd_card.cid);
    if (rc) goto fail;
    rc = sd_cmd48(3, 0, 6, &p);
    if (rc) goto fail;
    sd_card.rca = (uint16_t)(p >> 16);
    sd_slow = 0;

    /* CMD9: CSD (stand-by state), then CMD7 selects the card (transfer state). */
    rc = sd_cmd_r2(9, (uint32_t)sd_card.rca << 16, sd_card.csd);
    if (rc) goto fail;
    sd_card.blocks = sd_csd_blocks(sd_card.csd);
    rc = sd_cmd_r1(7, (uint32_t)sd_card.rca << 16, 0);
    if (rc) goto fail;

    /* ACMD6: 4-bit bus. CMD16: 512-byte blocks for byte-addressed cards. */
    rc = sd_acmd_r1(6, 2, 0);
    if (rc) goto fail;
    sd_card.bus4 = 1;
    if (!sd_card.block_addr) {
        rc = sd_cmd_r1(16, SD_BLOCK_SIZE, 0);
        if (rc) goto fail;
    }
    sd_card.ready = 1;
    return SD_OK;

fail:
    sd_slow = 0;
    sd_card.ready = 0;
    return rc;
}

/* ---- block transfer --------------------------------------------------------------- */

static int sd_check(uint32_t lba, uint32_t *addr)
{
    if (!sd_card.ready) return SD_ERR_NOINIT;
    if (sd_card.blocks && lba >= sd_card.blocks) return SD_ERR_RANGE;
    if (!sd_card.block_addr && lba >= 0x400000) return SD_ERR_RANGE;   /* 2 GB byte limit */
    *addr = sd_card.block_addr ? lba : lba * SD_BLOCK_SIZE;
    return SD_OK;
}

int sd_read_block(uint32_t lba, void *buf)
{
    uint32_t addr;
    int rc = sd_check(lba, &addr);
    if (rc) return rc;
    rc = sd_cmd_r1(17, addr, 0);
    if (rc) return rc;
    /* data start bit: DAT0 low */
    uint32_t wait = SD_READ_WAIT;
    while (sd_clk(SD_LO) & SDIO_DAT0)
        if (--wait == 0) return SD_ERR_TIMEOUT;
    sd4_read512(buf);
    sd_clocks(8);
    return SD_OK;
}

int sd_write_block(uint32_t lba, const void *buf)
{
    const uint8_t *p = buf;
    uint32_t addr;
    uint16_t crc[4];
    int rc = sd_check(lba, &addr);
    if (rc) return rc;
    rc = sd_cmd_r1(24, addr, 0);
    if (rc) return rc;
    sd_crc16x4(p, SD_BLOCK_SIZE, crc);
    uint8_t crcnib[16];                         /* one nibble per CRC clock, MSB first */
    for (int b = 15; b >= 0; b--)
        crcnib[15 - b] = (uint8_t)(((crc[3] >> b) & 1) << 3 | ((crc[2] >> b) & 1) << 2 |
                                   ((crc[1] >> b) & 1) << 1 | ((crc[0] >> b) & 1));
    sd_clocks(2);                               /* Nwr */
    sd4_write512(p, crcnib);                    /* start bit, data, CRCs, end bit */
    /* CRC status token on DAT0: start 0, then 010 = accepted, 101 = CRC error, end 1 */
    uint32_t wait = SD_TOKEN_WAIT;
    while (sd_clk(SD_LO) & SDIO_DAT0)
        if (--wait == 0) return SD_ERR_TIMEOUT;
    unsigned tok = 0;
    for (int i = 0; i < 3; i++) tok = (tok << 1) | ((sd_clk(SD_LO) >> SDIO_DAT0_BIT) & 1);
    sd_clk(SD_LO);                              /* end bit */
    if (tok != 2) return SD_ERR_TOKEN;
    /* busy: DAT0 low while the card programs the block */
    wait = SD_BUSY_WAIT;
    while (!(sd_clk(SD_LO) & SDIO_DAT0))
        if (--wait == 0) return SD_ERR_TIMEOUT;
    sd_clocks(8);
    return SD_OK;
}

/* ---- names ---------------------------------------------------------------------- */

const char *sd_strerror(int err)
{
    switch (err) {
    case SD_OK: return "ok";
    case SD_ERR_NOCARD: return "no card";
    case SD_ERR_TIMEOUT: return "timeout";
    case SD_ERR_CMD: return "command rejected";
    case SD_ERR_TOKEN: return "write rejected (CRC)";
    case SD_ERR_RANGE: return "block out of range";
    case SD_ERR_NOINIT: return "not initialised";
    case SD_ERR_VOLTAGE: return "voltage range";
    case SD_ERR_CRC: return "response CRC";
    default: return "?";
    }
}

const char *sd_type_name(enum sd_type t)
{
    switch (t) {
    case SD_V1: return "SD v1 (byte addressed)";
    case SD_V2: return "SD v2 SDSC (byte addressed)";
    case SD_V2_HC: return "SDHC/SDXC (block addressed)";
    default: return "none";
    }
}
