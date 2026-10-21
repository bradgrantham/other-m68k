/* Host-side unit test of the native-mode SD card model (emu/sdcard.c).
 *
 * A tiny bit-bang host drives the six SDIO lines exactly as the firmware will: for each
 * clock it lowers CLK with the outgoing CMD/DAT levels, samples the lines (the card has
 * updated them after the falling edge), then raises CLK (the card samples).  Prints one
 * "PASS:"/"FAIL:" line per check; exit status 1 on any failure. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hw.h"
#include "sdcard.h"

#define CLK  (1u << SDIO_CLK_BIT)
#define CMD  (1u << SDIO_CMD_BIT)
#define D0   (1u << SDIO_DAT0_BIT)
#define D1   (1u << SDIO_DAT1_BIT)
#define D2   (1u << SDIO_DAT2_BIT)
#define D3   (1u << SDIO_DAT3_BIT)
#define ALL  0x3Fu

static int fails;
static void check(const char *what, int ok)
{
    printf("%s: sd model: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails = 1;
}

/* one clock: drive `out` (CMD/DAT bits; 1 = released) with CLK low, sample, raise CLK */
static unsigned tick(unsigned out)
{
    out &= ~CLK;
    sd_bus(out);
    unsigned level = out & sd_lines();
    sd_bus(out | CLK);
    return level;
}

static unsigned dat_of(unsigned level)   /* DAT3..0 as a nibble */
{
    return ((level & D0) ? 1 : 0) | ((level & D1) ? 2 : 0) | ((level & D2) ? 4 : 0) | ((level & D3) ? 8 : 0);
}

static uint8_t crc7(const uint8_t *p, int n)
{
    uint8_t crc = 0;
    for (int i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x12 : (crc << 1));
    }
    return (uint8_t)(crc >> 1);
}

static uint16_t crc16_bit(uint16_t crc, int bit)
{
    int x = ((crc >> 15) & 1) ^ (bit & 1);
    crc = (uint16_t)(crc << 1);
    if (x) crc ^= 0x1021;
    return crc;
}

/* send a 48-bit command frame; crc_delta corrupts the CRC when non-zero */
static void send_cmd(unsigned cmd, uint32_t arg, int crc_delta)
{
    uint8_t f[6] = { (uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                     (uint8_t)(arg >> 8), (uint8_t)arg, 0 };
    f[5] = (uint8_t)(((crc7(f, 5) ^ crc_delta) << 1) | 1);
    for (int i = 0; i < 48; i++) {
        int bit = (f[i >> 3] >> (7 - (i & 7))) & 1;
        tick(bit ? ALL : (ALL & ~CMD));
    }
}

/* wait up to `limit` clocks for a start bit on CMD, then read nbits-1 more bits into resp
 * (MSB first).  Returns the number of clocks waited, or -1 on timeout. */
static int get_resp(uint8_t *resp, int nbits, int limit)
{
    int waited = 0;
    for (;;) {
        unsigned lv = tick(ALL);
        if (!(lv & CMD)) break;
        if (++waited > limit) return -1;
    }
    memset(resp, 0, (size_t)(nbits + 7) / 8);
    for (int i = 1; i < nbits; i++) {
        unsigned lv = tick(ALL);
        if (lv & CMD) resp[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    }
    return waited;
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* R1: index in resp[0], status in resp[1..4], CRC7 in resp[5] */
static int r1_ok(const uint8_t *r, unsigned cmd)
{
    return r[0] == cmd && (r[5] >> 1) == crc7(r, 5) && (r[5] & 1);
}

static uint8_t blk[512], rd[512];

/* read a block after a CMD17 whose R1 has been consumed; returns 1 if start seen, data and CRCs ok */
static int read_block(int bus4, int *crc_ok, int limit)
{
    int waited = 0;
    for (;;) {
        unsigned lv = tick(ALL);
        if (!(dat_of(lv) & 1)) break;                   /* start bit (DAT0 low) */
        if (++waited > limit) return 0;
    }
    uint16_t crc[4] = { 0, 0, 0, 0 }, got[4] = { 0, 0, 0, 0 };
    memset(rd, 0, sizeof rd);
    if (bus4) {
        for (int i = 0; i < 1024; i++) {
            unsigned n = dat_of(tick(ALL));
            rd[i >> 1] |= (uint8_t)((i & 1) ? n : n << 4);
            for (int l = 0; l < 4; l++) crc[l] = crc16_bit(crc[l], (n >> l) & 1);
        }
    } else {
        for (int i = 0; i < 4096; i++) {
            unsigned n = dat_of(tick(ALL));
            rd[i >> 3] |= (uint8_t)((n & 1) << (7 - (i & 7)));
            crc[0] = crc16_bit(crc[0], n & 1);
        }
    }
    for (int i = 0; i < 16; i++) {
        unsigned n = dat_of(tick(ALL));
        for (int l = 0; l < 4; l++) got[l] = (uint16_t)((got[l] << 1) | ((n >> l) & 1));
    }
    unsigned end = dat_of(tick(ALL));
    *crc_ok = (end & (bus4 ? 0xF : 1)) == (bus4 ? 0xF : 1);
    for (int l = 0; l < (bus4 ? 4 : 1); l++) if (crc[l] != got[l]) *crc_ok = 0;
    return 1;
}

/* write a block: start bit, data, CRC16s (corrupted when bad != 0), end bit; then wait for
 * the CRC status token on DAT0 and the end of busy.  Returns the 3 token bits or -1. */
static int write_block(int bus4, const uint8_t *data, int bad, int *busy_clocks)
{
    uint16_t crc[4] = { 0, 0, 0, 0 };
    tick(bus4 ? (ALL & ~(D0 | D1 | D2 | D3)) : (ALL & ~D0));          /* start bit */
    if (bus4) {
        for (int i = 0; i < 1024; i++) {
            unsigned n = (i & 1) ? (data[i >> 1] & 0xF) : (data[i >> 1] >> 4);
            unsigned out = ALL & ~((n & 1 ? 0 : D0) | (n & 2 ? 0 : D1) | (n & 4 ? 0 : D2) | (n & 8 ? 0 : D3));
            tick(out);
            for (int l = 0; l < 4; l++) crc[l] = crc16_bit(crc[l], (n >> l) & 1);
        }
    } else {
        for (int i = 0; i < 4096; i++) {
            int bit = (data[i >> 3] >> (7 - (i & 7))) & 1;
            tick(bit ? ALL : (ALL & ~D0));
            crc[0] = crc16_bit(crc[0], bit);
        }
    }
    if (bad) crc[0] ^= 0x0001;
    for (int b = 15; b >= 0; b--) {
        unsigned out = ALL;
        for (int l = 0; l < (bus4 ? 4 : 1); l++)
            if (!((crc[l] >> b) & 1)) out &= ~(1u << (l == 0 ? SDIO_DAT0_BIT : l == 1 ? SDIO_DAT1_BIT : l == 2 ? SDIO_DAT2_BIT : SDIO_DAT3_BIT));
        tick(out);
    }
    tick(ALL);                                                        /* end bit */
    int waited = 0;
    for (;;) {                                                        /* token start bit */
        if (!(dat_of(tick(ALL)) & 1)) break;
        if (++waited > 64) return -1;
    }
    int token = 0;
    for (int i = 0; i < 3; i++) token = (token << 1) | (dat_of(tick(ALL)) & 1);
    tick(ALL);                                                        /* token end bit */
    int busy = 0;
    while (!(dat_of(tick(ALL)) & 1)) { if (++busy > 1000) break; }
    *busy_clocks = busy;
    return token;
}

int main(void)
{
    char path[] = "/tmp/sdtest-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 1; }
    for (int b = 0; b < 2048; b++) {                 /* 1 MB image, block b filled with b-ish pattern */
        for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(b * 7 + i * 13);
        if (b == 0) memcpy(blk, "M68K-SD-TEST-BLOCK0", 19);
        if (pwrite(fd, blk, 512, (off_t)b * 512) != 512) { perror("pwrite"); return 1; }
    }
    check("image attached", sd_init(path, 0, 0) == 0);
    uint8_t r[17];
    unsigned lv;

    /* no card activity: lines read released */
    check("idle lines all released", (sd_lines() & ALL) == ALL);
    for (int i = 0; i < 80; i++) tick(ALL);

    /* CMD0: no response */
    send_cmd(0, 0, 0);
    check("CMD0 gets no response", get_resp(r, 48, 64) < 0);

    /* CMD8 -> R7 echo */
    send_cmd(8, 0x1AA, 0);
    int w = get_resp(r, 48, 64);
    check("CMD8 answered after Ncr = 8 clocks", w == 8);
    check("R7: index 8, echo 0x1AA, CRC7 valid", r[0] == 0x08 && be32(r + 1) == 0x1AA && r1_ok(r, 8));

    /* CMD55 + ACMD41 twice */
    send_cmd(55, 0, 0); get_resp(r, 48, 64);
    check("R1 to CMD55 has APP_CMD", r1_ok(r, 55) && (be32(r + 1) & (1u << 5)));
    send_cmd(41, 0x40000000, 0); get_resp(r, 48, 64);
    check("first ACMD41: R3 busy (bit 31 clear)", r[0] == 0x3F && !(be32(r + 1) & (1u << 31)) && r[5] == 0xFF);
    send_cmd(55, 0, 0); get_resp(r, 48, 64);
    send_cmd(41, 0x40000000, 0); get_resp(r, 48, 64);
    check("second ACMD41: ready with CCS", (be32(r + 1) & 0xC0000000) == 0xC0000000);

    /* CMD2 -> R2 CID (136 bits) */
    send_cmd(2, 0, 0);
    check("CMD2 answered", get_resp(r, 136, 64) >= 0);
    check("R2: header 3F, CID CRC7 valid, end bit", r[0] == 0x3F && (r[16] >> 1) == crc7(r + 1, 15) && (r[16] & 1));
    check("CID product name EMM68K", memcmp(r + 1 + 3, "M68K", 4) == 0 || memcmp(r + 1 + 1, "EMM68K", 6) == 0);

    /* CMD3 -> R6 */
    send_cmd(3, 0, 0); get_resp(r, 48, 64);
    unsigned rca = (unsigned)((r[1] << 8) | r[2]);
    check("R6: RCA 0x1234, CRC7 valid", r[0] == 0x03 && rca == 0x1234 && r1_ok(r, 3));

    /* CMD9 CSD in stby */
    send_cmd(9, rca << 16, 0);
    check("CMD9 answered", get_resp(r, 136, 64) >= 0);
    check("CSD v2, READ_BL_LEN 9, CRC7 valid", (r[1] >> 6) == 1 && (r[6] & 0xF) == 9 && (r[16] >> 1) == crc7(r + 1, 15));
    uint32_t c_size = ((uint32_t)(r[8] & 0x3F) << 16) | ((uint32_t)r[9] << 8) | r[10];
    check("CSD C_SIZE describes 1 MB", (c_size + 1) * 1024 == 2048);

    /* CMD7 select -> tran */
    send_cmd(7, rca << 16, 0); get_resp(r, 48, 64);
    check("CMD7: R1 shows state stby (3) at the command", r1_ok(r, 7) && ((be32(r + 1) >> 9) & 0xF) == 3);
    send_cmd(13, rca << 16, 0); get_resp(r, 48, 64);
    check("CMD13: now in tran (4), READY_FOR_DATA", ((be32(r + 1) >> 9) & 0xF) == 4 && (be32(r + 1) & (1u << 8)));

    /* 1-bit read of block 0 */
    send_cmd(17, 0, 0); get_resp(r, 48, 64);
    int crc_ok = 0;
    check("CMD17 (1-bit): R1 ok", r1_ok(r, 17) && !(be32(r + 1) & 0xFFF80000));
    check("CMD17 (1-bit): data block arrives", read_block(0, &crc_ok, 64));
    check("CMD17 (1-bit): block 0 contents", memcmp(rd, "M68K-SD-TEST-BLOCK0", 19) == 0);
    check("CMD17 (1-bit): CRC16 and end bit", crc_ok);

    /* ACMD6 -> 4-bit, read block 1 */
    send_cmd(55, 0, 0); get_resp(r, 48, 64);
    send_cmd(6, 2, 0); get_resp(r, 48, 64);
    check("ACMD6: R1 ok with APP_CMD", r1_ok(r, 6) && (be32(r + 1) & (1u << 5)));
    send_cmd(17, 1, 0); get_resp(r, 48, 64);
    check("CMD17 (4-bit): data block arrives", read_block(1, &crc_ok, 64));
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(1 * 7 + i * 13);
    check("CMD17 (4-bit): block 1 contents", memcmp(rd, blk, 512) == 0);
    check("CMD17 (4-bit): four CRC16s and end bits", crc_ok);

    /* CMD24 write block 5 (4-bit), read back, check image */
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(i ^ 0x5A);
    send_cmd(24, 5, 0); get_resp(r, 48, 64);
    check("CMD24: R1 ok", r1_ok(r, 24));
    int busy = 0;
    int tok = write_block(1, blk, 0, &busy);
    check("CMD24: CRC status token 010 (accepted)", tok == 2);
    check("CMD24: busy for Nbusy = 16 clocks", busy == 16);
    uint8_t img[512];
    pread(fd, img, 512, 5 * 512);
    check("CMD24: block 5 written to the image", memcmp(img, blk, 512) == 0);
    send_cmd(17, 5, 0); get_resp(r, 48, 64);
    read_block(1, &crc_ok, 64);
    check("CMD17: block 5 reads back", memcmp(rd, blk, 512) == 0 && crc_ok);

    /* CMD24 with a bad CRC on DAT0 */
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(0xA5 - i);
    send_cmd(24, 6, 0); get_resp(r, 48, 64);
    tok = write_block(1, blk, 1, &busy);
    check("CMD24 bad CRC: status token 101", tok == 5);
    pread(fd, img, 512, 6 * 512);
    check("CMD24 bad CRC: block 6 unchanged", memcmp(img, blk, 512) != 0);
    send_cmd(13, rca << 16, 0); get_resp(r, 48, 64);
    check("CMD13 after the bad write: back in tran", ((be32(r + 1) >> 9) & 0xF) == 4);

    /* 1-bit write */
    send_cmd(55, 0, 0); get_resp(r, 48, 64);
    send_cmd(6, 0, 0); get_resp(r, 48, 64);
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(i * 3);
    send_cmd(24, 7, 0); get_resp(r, 48, 64);
    tok = write_block(0, blk, 0, &busy);
    pread(fd, img, 512, 7 * 512);
    check("CMD24 (1-bit): accepted and written", tok == 2 && memcmp(img, blk, 512) == 0);

    /* bad CRC7 command: no response, COM_CRC_ERROR in the next one */
    send_cmd(13, rca << 16, 0x15);
    check("bad CRC7: no response", get_resp(r, 48, 64) < 0);
    send_cmd(13, rca << 16, 0); get_resp(r, 48, 64);
    check("next R1 reports COM_CRC_ERROR", be32(r + 1) & (1u << 23));
    send_cmd(13, rca << 16, 0); get_resp(r, 48, 64);
    check("COM_CRC_ERROR cleared by that response", !(be32(r + 1) & (1u << 23)));

    /* illegal command */
    send_cmd(60, 0, 0);
    check("CMD60: no response", get_resp(r, 48, 64) < 0);
    send_cmd(13, rca << 16, 0); get_resp(r, 48, 64);
    check("next R1 reports ILLEGAL_COMMAND", be32(r + 1) & (1u << 22));

    /* out of range read */
    send_cmd(17, 2048, 0); get_resp(r, 48, 64);
    check("CMD17 past the image: OUT_OF_RANGE, no data", (be32(r + 1) & (1u << 31)) && !read_block(0, &crc_ok, 100));

    /* CMD16 with a bad length */
    send_cmd(16, 1024, 0); get_resp(r, 48, 64);
    check("CMD16 1024: BLOCK_LEN_ERROR", be32(r + 1) & (1u << 29));

    /* CMD18 two blocks then CMD12 (4-bit) */
    send_cmd(55, 0, 0); get_resp(r, 48, 64);
    send_cmd(6, 2, 0); get_resp(r, 48, 64);
    send_cmd(18, 2, 0); get_resp(r, 48, 64);
    int ok18 = read_block(1, &crc_ok, 64) && crc_ok;
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(2 * 7 + i * 13);
    ok18 = ok18 && memcmp(rd, blk, 512) == 0;
    ok18 = ok18 && read_block(1, &crc_ok, 64) && crc_ok;
    for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(3 * 7 + i * 13);
    ok18 = ok18 && memcmp(rd, blk, 512) == 0;
    check("CMD18: blocks 2 and 3 stream back", ok18);
    send_cmd(12, 0, 0); get_resp(r, 48, 64);
    check("CMD12: R1 ok", r1_ok(r, 12));
    for (int i = 0; i < 100; i++) lv = tick(ALL);
    check("after CMD12 the DAT lines are released", dat_of(lv) == 0xF);

    /* a second CMD0 brings the card back to idle; CMD8 works again */
    send_cmd(0, 0, 0);
    send_cmd(8, 0x1AA, 0); get_resp(r, 48, 64);
    check("CMD0 then CMD8 again: R7", r[0] == 0x08 && be32(r + 1) == 0x1AA);

    sd_shutdown();
    unlink(path);
    close(fd);
    return fails;
}
