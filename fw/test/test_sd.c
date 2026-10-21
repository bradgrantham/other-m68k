/* SD driver against the emulator's native-mode card model (emu/sdcard.c, compiled into the
 * test with its lifecycle functions renamed emu_sd_*): sd.c's register accesses are routed
 * to sd_bus()/sd_lines(), so the whole bit-banged protocol (CRC7 frames, R1/R2/R3/R6/R7,
 * ACMD41 polling, 4-bit data with CRC16s, CRC status token, busy) is exercised end to end. */
#include <unistd.h>
#include "test.h"
#include "sd.h"

int  emu_sd_init(const char *image_path, int trace, int sdsc);
void emu_sd_shutdown(void);
void emu_sd_reset(void);
void sd_bus(unsigned host_drive);
unsigned sd_lines(void);

static unsigned host_drive = 0x3F;
static unsigned long accesses;

void sdt_write(uint16_t drive)
{
    host_drive = drive & 0x3F;
    accesses++;
    sd_bus(host_drive);
}

uint16_t sdt_read(void)
{
    accesses++;
    return (uint16_t)(host_drive & sd_lines());
}

/* reference bit-serial CRC16 of line L of a 4-bit block */
static uint16_t ref_crc16_line(const uint8_t *d, int n, int line)
{
    uint16_t crc = 0;
    for (int i = 0; i < 2 * n; i++) {
        unsigned nib = (i & 1) ? (d[i >> 1] & 15) : (d[i >> 1] >> 4);
        int x = ((crc >> 15) & 1) ^ ((nib >> line) & 1);
        crc = (uint16_t)(crc << 1);
        if (x) crc ^= 0x1021;
    }
    return crc;
}

static char img_path[64];

static int make_image(size_t bytes)
{
    strcpy(img_path, "/tmp/fwtest-sd-XXXXXX");
    int fd = mkstemp(img_path);
    if (fd < 0) return -1;
    uint8_t blk[512];
    for (size_t b = 0; b < bytes / 512; b++) {
        for (int i = 0; i < 512; i++) blk[i] = (uint8_t)(b * 31 + i);
        if (write(fd, blk, 512) != 512) { close(fd); return -1; }
    }
    close(fd);
    return 0;
}

static int image_byte(long off)
{
    FILE *f = fopen(img_path, "rb");
    if (!f) return -1;
    fseek(f, off, SEEK_SET);
    int c = fgetc(f);
    fclose(f);
    return c;
}

void test_sd(void)
{
    uint8_t buf[512], crcbuf[512];
    uint16_t crc[4];

    /* CRC7 known values: CMD0 arg 0 -> 0x95, CMD8 arg 0x1AA -> 0x87 */
    uint8_t c0[5] = { 0x40, 0, 0, 0, 0 }, c8[5] = { 0x48, 0, 0, 0x01, 0xAA };
    CHECK_EQ(sd_crc7(c0, 5), 0x95);
    CHECK_EQ(sd_crc7(c8, 5), 0x87);

    /* CRC16 x4 against the bit-serial reference */
    for (int i = 0; i < 512; i++) crcbuf[i] = (uint8_t)(i * 193 + (i >> 3));
    sd_crc16x4(crcbuf, 512, crc);
    for (int l = 0; l < 4; l++) CHECK_EQ(crc[l], ref_crc16_line(crcbuf, 512, l));
    memset(crcbuf, 0xFF, 512);
    sd_crc16x4(crcbuf, 512, crc);
    for (int l = 0; l < 4; l++) CHECK_EQ(crc[l], ref_crc16_line(crcbuf, 512, l));
    memset(crcbuf, 0, 512);
    sd_crc16x4(crcbuf, 512, crc);
    CHECK_EQ(crc[0], 0); CHECK_EQ(crc[3], 0);

    /* CSD v1 capacity formula: 2 GB card (C_SIZE 4095, MULT 7, BL_LEN 10) */
    uint8_t csd[16] = {0};
    csd[5] = 0x0A; csd[6] = 0x03; csd[7] = 0xFF; csd[8] = 0xC0; csd[9] = 0x03; csd[10] = 0x80;
    CHECK_EQ(sd_csd_blocks(csd), 4096u * 512 * 2);

    /* no card: CMD55 never answers */
    emu_sd_init(NULL, 0, 0);
    CHECK_EQ(sd_init(), SD_ERR_NOCARD);
    CHECK(!sd_card.ready);
    CHECK_EQ(sd_read_block(0, buf), SD_ERR_NOINIT);
    CHECK_STR(sd_strerror(SD_ERR_NOCARD), "no card");

    /* SDHC: a 1 MB image (2048 blocks) */
    if (make_image(1024 * 1024) != 0) { printf("test_sd: cannot create image, skipping\n"); return; }
    CHECK_EQ(emu_sd_init(img_path, 0, 0), 0);
    CHECK_EQ(sd_init(), SD_OK);
    CHECK(sd_card.ready);
    CHECK_EQ(sd_card.type, SD_V2_HC);
    CHECK_EQ(sd_card.block_addr, 1);
    CHECK_EQ(sd_card.bus4, 1);
    CHECK_EQ(sd_card.rca, 0x1234);
    CHECK_EQ(sd_card.blocks, 2048);
    CHECK_EQ(sd_card.ocr[0], 0xC0);
    CHECK_EQ(sd_card.cid[0], 0x01);                         /* MID */
    CHECK_EQ(sd_card.cid[1], 'E'); CHECK_EQ(sd_card.cid[2], 'M');
    CHECK_EQ(sd_card.csd[0] >> 6, 1);                       /* CSD v2 */
    CHECK_STR(sd_type_name(sd_card.type), "SDHC/SDXC (block addressed)");

    /* reads: block pattern b*31+i */
    CHECK_EQ(sd_read_block(0, buf), SD_OK);
    CHECK_EQ(buf[0], 0); CHECK_EQ(buf[1], 1); CHECK_EQ(buf[511], 255);
    CHECK_EQ(sd_read_block(7, buf), SD_OK);
    CHECK_EQ(buf[0], (uint8_t)(7 * 31)); CHECK_EQ(buf[100], (uint8_t)(7 * 31 + 100));
    accesses = 0;
    CHECK_EQ(sd_read_block(2047, buf), SD_OK);
    CHECK_EQ(buf[511], (uint8_t)(2047 * 31 + 511));
    CHECK(accesses < 4000);                                 /* ~3 accesses per clock, ~1150 clocks */
    /* out of range: our own check (CSD) first, then the card's OUT_OF_RANGE */
    CHECK_EQ(sd_read_block(2048, buf), SD_ERR_RANGE);
    sd_card.blocks = 0;
    CHECK_EQ(sd_read_block(5000, buf), SD_ERR_RANGE);
    sd_card.blocks = 2048;
    CHECK_EQ(sd_read_block(3, buf), SD_OK);                 /* still in the transfer state */
    CHECK_EQ(buf[0], (uint8_t)(3 * 31));

    /* write then read back (the card checks the four CRC16s) */
    for (int i = 0; i < 512; i++) buf[i] = (uint8_t)(i * 7 + 3);
    CHECK_EQ(sd_write_block(5, buf), SD_OK);
    CHECK_EQ(image_byte(5 * 512 + 3), (uint8_t)(3 * 7 + 3));
    CHECK_EQ(image_byte(5 * 512 + 511), (uint8_t)(511 * 7 + 3));
    memset(buf, 0, sizeof buf);
    CHECK_EQ(sd_read_block(5, buf), SD_OK);
    CHECK_EQ(buf[3], (uint8_t)(3 * 7 + 3)); CHECK_EQ(buf[511], (uint8_t)(511 * 7 + 3));
    CHECK_EQ(sd_read_block(4, buf), SD_OK);                 /* neighbours untouched */
    CHECK_EQ(buf[0], (uint8_t)(4 * 31));
    CHECK_EQ(sd_write_block(2048, buf), SD_ERR_RANGE);
    sd_card.blocks = 0;
    CHECK_EQ(sd_write_block(9999, buf), SD_ERR_RANGE);
    sd_card.blocks = 2048;
    for (int i = 0; i < 512; i++) buf[i] = (uint8_t)(0xA5 ^ i);
    CHECK_EQ(sd_write_block(0, buf), SD_OK);
    CHECK_EQ(image_byte(0), 0xA5);
    CHECK_EQ(sd_read_block(0, buf), SD_OK);
    CHECK_EQ(buf[1], 0xA4);

    /* re-initialisation works from the transfer state (CMD0 resets the card) */
    CHECK_EQ(sd_init(), SD_OK);
    CHECK_EQ(sd_read_block(1, buf), SD_OK);
    CHECK_EQ(buf[0], 31);
    emu_sd_shutdown();
    unlink(img_path);

    /* SDSC: byte addressed, CSD v1, CMD16; image a multiple of 256 KB */
    if (make_image(512 * 1024) != 0) { printf("test_sd: cannot create image, skipping\n"); return; }
    CHECK_EQ(emu_sd_init(img_path, 0, 1), 0);
    CHECK_EQ(sd_init(), SD_OK);
    CHECK_EQ(sd_card.type, SD_V2);
    CHECK_EQ(sd_card.block_addr, 0);
    CHECK_EQ(sd_card.bus4, 1);
    CHECK_EQ(sd_card.blocks, 1024);
    CHECK_EQ(sd_card.csd[0] >> 6, 0);
    CHECK_EQ(sd_read_block(1, buf), SD_OK);
    CHECK_EQ(buf[0], 31); CHECK_EQ(buf[511], (uint8_t)(31 + 511));
    for (int i = 0; i < 512; i++) buf[i] = (uint8_t)(i + 99);
    CHECK_EQ(sd_write_block(1023, buf), SD_OK);
    CHECK_EQ(image_byte(1023L * 512 + 10), (uint8_t)(10 + 99));
    CHECK_EQ(sd_read_block(1024, buf), SD_ERR_RANGE);
    emu_sd_shutdown();
    unlink(img_path);
}
