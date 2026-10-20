/* SD card driver: SPI mode 0 bit-banged on SDIO.
 *
 * Wire protocol (SD Physical Layer Simplified Spec, chapter 7): CS low, 6-byte command
 * (0x40|cmd, 32-bit argument, CRC7<<1|1), R1 response within 8 bytes (Ncr), optional
 * R3/R7 trailer, data blocks with a 0xFE start token and a CRC16 the card checks only
 * after CMD59 (CRC stays off; the command CRC7 is always computed, which is cheap and
 * lets a card that insists on CRC accept us).
 *
 * Pin level: SDIO bit 0 = CS (0 = selected), bit 1 = CLK, bit 2 = MOSI; MISO is
 * INTSTAT.SDMISO. Mode 0: MOSI is set while CLK is low, the card samples it on the rising
 * edge and drives the next MISO bit after the falling edge, so we read MISO after our own
 * rising edge. Each register access is one bus cycle, so a bit costs two writes and one
 * read (about 50 CPU clocks, ~250 kbit/s at full speed). Identification (the 74+ idle
 * clocks up to the end of ACMD41) runs with extra delays to stay under the 400 kHz the spec
 * demands there. Data blocks come in through sdhw_read (sdspi.S, 44 clocks per bit). Only
 * the monitor and the TRAP #15 handlers call this; no ISR touches SDIO, so no interrupt
 * masking is needed. */
#include "bios.h"
#include "sd.h"

uint16_t sdio_shadow = IO_SDIO_RESET;
struct sd_info sd_card;

/* data-token wait and busy-poll limits, in bytes exchanged (~4 us each at full speed) */
#define SD_READ_TOKEN_WAIT   40000      /* ~160 ms; spec: 100 ms read access time */
#define SD_WRITE_BUSY_WAIT   125000     /* ~500 ms; spec: 250 ms write busy */
#define SD_INIT_POLLS        2000       /* ACMD41 iterations (~1 s at 400 kHz) */

/* ---- CRC7 (x^7 + x^3 + 1) ------------------------------------------------------- */

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

/* ---- pin level -------------------------------------------------------------------- */
#ifndef HOST_TEST

static int sd_slow;

void sd_hw_init(void)
{
    sdio_shadow = SDIO_CS;              /* deselected, CLK low (mode 0 idle), MOSI low */
    IO_SDIO = sdio_shadow;
}

void sdhw_slow(int on) { sd_slow = on; }

void sdhw_select(int on)
{
    /* CS changes while CLK is low */
    sdio_shadow = (uint16_t)((sdio_shadow & SDIO_MOSI) | (on ? 0 : SDIO_CS));
    IO_SDIO = sdio_shadow;
}

/* One bit: MOSI valid with CLK low, rising edge, then sample MISO (the card put this bit
 * on MISO after the previous falling edge). */
#define SD_BIT(mask) \
    v = (uint16_t)(base | ((out & (mask)) ? SDIO_MOSI : 0)); \
    IO_SDIO = v; \
    IO_SDIO = (uint16_t)(v | SDIO_CLK); \
    in = (uint8_t)((in << 1) | ((IO_INTSTAT >> INTSTAT_SDMISO_BIT) & 1));

uint8_t sdhw_xfer(uint8_t out)
{
    uint16_t base = (uint16_t)(sdio_shadow & SDIO_CS);     /* keep CS, CLK/MOSI low */
    uint16_t v;
    uint8_t in = 0;
    if (sd_slow) {
        for (int i = 0; i < 8; i++) {
            v = (uint16_t)(base | ((out & 0x80) ? SDIO_MOSI : 0));
            IO_SDIO = v;
            delay_loops(8);                     /* >= 1.25 us per half period */
            IO_SDIO = (uint16_t)(v | SDIO_CLK);
            delay_loops(8);
            in = (uint8_t)((in << 1) | ((IO_INTSTAT >> INTSTAT_SDMISO_BIT) & 1));
            out = (uint8_t)(out << 1);
        }
    } else {
        SD_BIT(0x80) SD_BIT(0x40) SD_BIT(0x20) SD_BIT(0x10)
        SD_BIT(0x08) SD_BIT(0x04) SD_BIT(0x02) SD_BIT(0x01)
    }
    IO_SDIO = base;                             /* falling edge: card presents its next bit */
    sdio_shadow = base;
    return in;
}

#endif /* !HOST_TEST */

/* ---- command layer ---------------------------------------------------------------- */

static void sd_end(void)
{
    sdhw_select(0);
    sdhw_xfer(0xFF);                            /* 8 clocks with CS high release MISO */
}

/* Send a command and return R1 (bit 7 clear), or 0xFF if nothing came within Ncr. The
 * card stays selected so the caller can read R3/R7 trailers or a data block. */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t buf[6];
    buf[0] = (uint8_t)(0x40 | cmd);
    buf[1] = (uint8_t)(arg >> 24);
    buf[2] = (uint8_t)(arg >> 16);
    buf[3] = (uint8_t)(arg >> 8);
    buf[4] = (uint8_t)arg;
    buf[5] = sd_crc7(buf, 5);
    sdhw_select(1);
    sdhw_xfer(0xFF);                            /* Ncs: at least one idle byte */
    for (int i = 0; i < 6; i++) sdhw_xfer(buf[i]);
    if (cmd == 12) sdhw_xfer(0xFF);             /* CMD12 has a stuff byte before R1 */
    for (int i = 0; i < 8; i++) {
        uint8_t r = sdhw_xfer(0xFF);
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

static uint8_t sd_cmd_done(uint8_t cmd, uint32_t arg)
{
    uint8_t r = sd_cmd(cmd, arg);
    sd_end();
    return r;
}

static int sd_r1_error(uint8_t r1)
{
    return r1 == 0xFF ? SD_ERR_TIMEOUT : SD_ERR_CMD;
}

/* Wait for the 0xFE start token, then read n bytes and the 2 CRC bytes. Card selected. */
static int sd_read_data(uint8_t *buf, int n, uint32_t wait)
{
    uint8_t t;
    do {
        t = sdhw_xfer(0xFF);
        if (t == 0xFE) break;
        if ((t & 0xF0) == 0) return SD_ERR_TOKEN;   /* data error token (0x08 = range) */
    } while (--wait);
    if (t != 0xFE) return SD_ERR_TIMEOUT;
    sdhw_read(buf, n);
    sdhw_xfer(0xFF);
    sdhw_xfer(0xFF);                            /* CRC16, not checked (CRC is off) */
    return SD_OK;
}

static int sd_read_register(uint8_t cmd, uint8_t *out16)
{
    uint8_t r = sd_cmd(cmd, 0);
    int rc;
    if (r != 0) rc = sd_r1_error(r);
    else rc = sd_read_data(out16, 16, 1000);
    sd_end();
    return rc;
}

/* ---- initialisation --------------------------------------------------------------- */

int sd_init(void)
{
    uint8_t r;
    int rc;

    memset(&sd_card, 0, sizeof sd_card);
    sd_hw_init();
    sdhw_slow(1);
    sdhw_select(0);
    for (int i = 0; i < 10; i++) sdhw_xfer(0xFF);      /* >= 74 clocks with CS high */

    /* CMD0: software reset into SPI mode (CRC 0x95 with arg 0). */
    r = 0xFF;
    for (int i = 0; i < 8 && r != 0x01; i++) r = sd_cmd_done(0, 0);
    if (r != 0x01) { rc = SD_ERR_NOCARD; goto fail; }

    /* CMD8: voltage check; v2 cards answer R7 echoing the pattern, v1 cards reject it. */
    r = sd_cmd(8, 0x000001AA);
    if (r == 0x01) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = sdhw_xfer(0xFF);
        sd_end();
        if ((r7[2] & 0x0F) != 0x01 || r7[3] != 0xAA) { rc = SD_ERR_VOLTAGE; goto fail; }
        sd_card.type = SD_V2;
    } else {
        sd_end();
        if (r == 0xFF || !(r & 0x04)) { rc = sd_r1_error(r); goto fail; }
        sd_card.type = SD_V1;
    }

    /* ACMD41 (HCS set for v2) until the card leaves the idle state; cards that reject
     * ACMD41 (old MMC) get CMD1. */
    {
        int use_cmd1 = 0, polls = SD_INIT_POLLS;
        uint32_t arg = sd_card.type == SD_V2 ? 0x40000000 : 0;
        for (;;) {
            if (!use_cmd1) {
                r = sd_cmd_done(55, 0);
                if (r == 0xFF) { rc = SD_ERR_TIMEOUT; goto fail; }
                if (r & 0x04) use_cmd1 = 1;
                else r = sd_cmd_done(41, arg);
            }
            if (use_cmd1) r = sd_cmd_done(1, 0);
            if (r == 0x00) break;
            if (r != 0x01) { rc = sd_r1_error(r); goto fail; }
            if (--polls == 0) { rc = SD_ERR_TIMEOUT; goto fail; }
        }
    }

    /* CMD58: OCR, bit 30 (CCS) = block addressing (SDHC/SDXC). */
    if (sd_card.type == SD_V2) {
        r = sd_cmd(58, 0);
        if (r != 0) { sd_end(); rc = sd_r1_error(r); goto fail; }
        for (int i = 0; i < 4; i++) sd_card.ocr[i] = sdhw_xfer(0xFF);
        sd_end();
        if (sd_card.ocr[0] & 0x40) { sd_card.type = SD_V2_HC; sd_card.block_addr = 1; }
    }
    if (!sd_card.block_addr) {
        r = sd_cmd_done(16, SD_BLOCK_SIZE);     /* byte addressed: fix the block length */
        if (r != 0) { rc = sd_r1_error(r); goto fail; }
    }

    sdhw_slow(0);
    rc = sd_read_register(9, sd_card.csd);      /* CSD: capacity */
    if (rc) goto fail;
    rc = sd_read_register(10, sd_card.cid);     /* CID: manufacturer, name, serial */
    if (rc) goto fail;
    sd_card.blocks = sd_csd_blocks(sd_card.csd);
    sd_card.ready = 1;
    return SD_OK;

fail:
    sdhw_slow(0);
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
    uint8_t r = sd_cmd(17, addr);
    if (r != 0) rc = sd_r1_error(r);
    else rc = sd_read_data(buf, SD_BLOCK_SIZE, SD_READ_TOKEN_WAIT);
    sd_end();
    return rc;
}

int sd_write_block(uint32_t lba, const void *buf)
{
    const uint8_t *p = buf;
    uint32_t addr;
    int rc = sd_check(lba, &addr);
    if (rc) return rc;
    uint8_t r = sd_cmd(24, addr);
    if (r != 0) { sd_end(); return sd_r1_error(r); }
    sdhw_xfer(0xFF);                            /* Nwr */
    sdhw_xfer(0xFE);                            /* start token */
    for (int i = 0; i < SD_BLOCK_SIZE; i++) sdhw_xfer(p[i]);
    sdhw_xfer(0xFF);
    sdhw_xfer(0xFF);                            /* CRC16 placeholder */
    /* data response token xxx0sss1: 010 accepted, 101 CRC error, 110 write error */
    r = 0xFF;
    for (int i = 0; i < 8 && r == 0xFF; i++) r = sdhw_xfer(0xFF);
    if ((r & 0x1F) != 0x05) { sd_end(); return r == 0xFF ? SD_ERR_TIMEOUT : SD_ERR_TOKEN; }
    /* busy: MISO low until the card has programmed the block */
    uint32_t wait = SD_WRITE_BUSY_WAIT;
    while (sdhw_xfer(0xFF) != 0xFF)
        if (--wait == 0) { sd_end(); return SD_ERR_TIMEOUT; }
    sd_end();
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
    case SD_ERR_TOKEN: return "data error";
    case SD_ERR_RANGE: return "block out of range";
    case SD_ERR_NOINIT: return "not initialised";
    case SD_ERR_VOLTAGE: return "voltage range";
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
