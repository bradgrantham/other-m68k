/* SD card (SDHC, SPI mode) behind the SDIO bit-bang register.
 *
 * SPI mode 0 slave: MOSI is sampled on the rising CLK edge, MISO is updated
 * after the falling edge so it is stable around the next rising edge.  Every
 * eight clocks a byte is exchanged with the card state machine; the byte the
 * card returns is what the master will read during the *next* eight clocks,
 * exactly as on the wire.  CS high (or no --sd image) reads 0xFF.
 *
 * Commands: CMD0, 1, 8 (R7), 9 (CSD v2), 10 (CID), 12, 13, 16, 17, 18, 24,
 * 25, 55, ACMD41 (idle on the first poll, ready afterwards), 58 (OCR, CCS=1),
 * 59.  Block addressing; the image is any multiple of 512 bytes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "emu.h"
#include "sdcard.h"

static int fd = -1;
static int present;
static int trace;
static uint32_t nblocks;

/* pins */
static int cs_active, prev_clk, miso = 1;
static unsigned in_shift, bitcnt;
static uint8_t out_byte = 0xFF;

/* protocol */
enum { M_CMD, M_READ_MULTI, M_WRITE_TOKEN, M_WRITE_DATA };
static int mode;
static uint8_t cmd_buf[6];
static int cmd_len;
static uint8_t resp[1 + 1 + 4 + 512 + 2 + 4];
static int resp_len, resp_idx;
static int idle = 1, acmd, init_polls;
static uint32_t multi_lba, write_lba;
static int multi_write;
static uint8_t wbuf[514];
static int wlen;

/* ---- CRCs ------------------------------------------------------------------ */

static uint8_t crc7(const uint8_t *p, int n)
{
    uint8_t crc = 0;
    for (int i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x12 : (crc << 1));
    }
    return (uint8_t)((crc & 0xFE) | 1);        /* already shifted left by one */
}

static uint16_t crc16(const uint8_t *p, int n)
{
    uint16_t crc = 0;
    for (int i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1));
    }
    return crc;
}

/* ---- response queue -------------------------------------------------------- */

static void q_clear(void)
{
    resp_len = resp_idx = 0;
}

static void q_byte(uint8_t b)
{
    if (resp_len < (int)sizeof resp)
        resp[resp_len++] = b;
}

static void q_block(const uint8_t *data, int n)
{
    q_byte(0xFE);
    for (int i = 0; i < n; i++)
        q_byte(data[i]);
    uint16_t c = crc16(data, n);
    q_byte((uint8_t)(c >> 8));
    q_byte((uint8_t)c);
}

static void q_read_block(uint32_t lba)
{
    uint8_t buf[512];
    q_byte(0xFF);                               /* access time */
    if (lba >= nblocks) {
        q_byte(0x08);                           /* data error token: out of range */
        return;
    }
    ssize_t n = pread(fd, buf, sizeof buf, (off_t)lba * 512);
    if (n != (ssize_t)sizeof buf) {
        fprintf(stderr, "m68k-emu: sd: read of block %u failed\n", lba);
        q_byte(0x02);                           /* CC error */
        return;
    }
    q_block(buf, 512);
}

static void make_csd(uint8_t *csd)
{
    uint32_t c_size = (nblocks + 1023) / 1024;  /* units of 512 KB */
    if (c_size) c_size--;
    memset(csd, 0, 16);
    csd[0] = 0x40;                              /* CSD_STRUCTURE = 1 (v2) */
    csd[1] = 0x0E;                              /* TAAC */
    csd[2] = 0x00;                              /* NSAC */
    csd[3] = 0x32;                              /* TRAN_SPEED 25 MHz */
    csd[4] = 0x5B;                              /* CCC */
    csd[5] = 0x59;                              /* CCC, READ_BL_LEN = 9 */
    csd[6] = 0x00;
    csd[7] = (uint8_t)((c_size >> 16) & 0x3F);
    csd[8] = (uint8_t)(c_size >> 8);
    csd[9] = (uint8_t)c_size;
    csd[10] = 0x7F;                             /* ERASE_BLK_EN, SECTOR_SIZE */
    csd[11] = 0x80;
    csd[12] = 0x0A;                             /* R2W_FACTOR, WRITE_BL_LEN = 9 */
    csd[13] = 0x40;
    csd[14] = 0x00;
    csd[15] = crc7(csd, 15);
}

static void make_cid(uint8_t *cid)
{
    static const uint8_t base[15] = {
        0x01, 'E', 'M', 'M', '6', '8', 'K', '0',    /* MID, OID, PNM */
        0x10,                                       /* PRV 1.0 */
        0x00, 0x00, 0x68, 0x00,                     /* PSN */
        0x01, 0x8A,                                 /* MDT 2024-10 */
    };
    memcpy(cid, base, 15);
    cid[15] = crc7(cid, 15);
}

/* ---- command handling -------------------------------------------------------- */

static void handle_command(void)
{
    unsigned cmd = cmd_buf[0] & 0x3F;
    uint32_t arg = ((uint32_t)cmd_buf[1] << 24) | ((uint32_t)cmd_buf[2] << 16) |
                   ((uint32_t)cmd_buf[3] << 8) | cmd_buf[4];
    uint8_t r1 = idle ? 0x01 : 0x00;
    int was_acmd = acmd;
    acmd = 0;

    q_clear();
    q_byte(0xFF);                               /* Ncr */

    if (was_acmd && cmd == 41) {
        if (++init_polls >= 2)
            idle = 0;
        r1 = idle ? 0x01 : 0x00;
        q_byte(r1);
        goto done;
    }

    switch (cmd) {
    case 0:
        idle = 1;
        init_polls = 0;
        mode = M_CMD;
        r1 = 0x01;
        q_byte(r1);
        break;
    case 1:
        if (++init_polls >= 2)
            idle = 0;
        r1 = idle ? 0x01 : 0x00;
        q_byte(r1);
        break;
    case 8:
        q_byte(r1);
        q_byte(0x00); q_byte(0x00); q_byte(0x01); q_byte((uint8_t)arg);
        break;
    case 9: {
        uint8_t csd[16];
        make_csd(csd);
        q_byte(r1);
        q_byte(0xFF);
        q_block(csd, 16);
        break;
    }
    case 10: {
        uint8_t cid[16];
        make_cid(cid);
        q_byte(r1);
        q_byte(0xFF);
        q_block(cid, 16);
        break;
    }
    case 12:
        mode = M_CMD;
        q_clear();
        q_byte(0xFF);                           /* stuff byte */
        q_byte(r1);
        q_byte(0x00);                           /* busy */
        break;
    case 13:
        q_byte(r1);
        q_byte(0x00);
        break;
    case 16:
        if (arg != 512)
            r1 |= 0x40;
        q_byte(r1);
        break;
    case 17:
        q_byte(r1);
        q_read_block(arg);
        break;
    case 18:
        q_byte(r1);
        q_read_block(arg);
        multi_lba = arg + 1;
        mode = M_READ_MULTI;
        break;
    case 24:
    case 25:
        if (arg >= nblocks) {
            r1 |= 0x40;
            q_byte(r1);
            break;
        }
        q_byte(r1);
        write_lba = arg;
        multi_write = (cmd == 25);
        mode = M_WRITE_TOKEN;
        break;
    case 55:
        acmd = 1;
        q_byte(r1);
        break;
    case 58:
        q_byte(r1);
        if (idle) {
            q_byte(0x00); q_byte(0xFF); q_byte(0x80); q_byte(0x00);
        } else {
            q_byte(0xC0); q_byte(0xFF); q_byte(0x80); q_byte(0x00);
        }
        break;
    case 59:
        q_byte(r1);
        break;
    default:
        r1 |= 0x04;
        q_byte(r1);
        break;
    }
done:
    if (trace)
        fprintf(stderr, "m68k-emu: sd: %sCMD%u arg=%08X -> R1=%02X\n",
                was_acmd ? "A" : "", cmd, arg, r1);
}

static void write_block_done(void)
{
    uint8_t token;
    if (write_lba >= nblocks) {
        token = 0x0D;                           /* write error */
    } else if (pwrite(fd, wbuf, 512, (off_t)write_lba * 512) != 512) {
        fprintf(stderr, "m68k-emu: sd: write of block %u failed: %s\n", write_lba, strerror(errno));
        token = 0x0D;
    } else {
        token = 0x05;                           /* data accepted */
    }
    if (trace)
        fprintf(stderr, "m68k-emu: sd: wrote block %u (token %02X)\n", write_lba, token);
    write_lba++;
    q_clear();
    q_byte(token);
    q_byte(0x00); q_byte(0x00);                 /* busy */
    mode = multi_write ? M_WRITE_TOKEN : M_CMD;
}

static int is_command_start(uint8_t in)
{
    return (in & 0xC0) == 0x40;
}

static uint8_t exchange(uint8_t in)
{
    switch (mode) {
    case M_WRITE_TOKEN:
        if (in == 0xFE || in == 0xFC) {
            mode = M_WRITE_DATA;
            wlen = 0;
        } else if (in == 0xFD) {                /* stop transmission token */
            mode = M_CMD;
            q_clear();
            q_byte(0xFF);
            q_byte(0x00); q_byte(0x00);         /* busy */
        } else if (is_command_start(in) && cmd_len == 0) {
            mode = M_CMD;
            cmd_buf[cmd_len++] = in;
        }
        break;
    case M_WRITE_DATA:
        wbuf[wlen++] = in;
        if (wlen == 514)
            write_block_done();
        break;
    default:
        if (cmd_len == 0) {
            if (is_command_start(in))
                cmd_buf[cmd_len++] = in;
        } else {
            cmd_buf[cmd_len++] = in;
            if (cmd_len == 6) {
                cmd_len = 0;
                handle_command();
            }
        }
        break;
    }

    if (resp_idx < resp_len)
        return resp[resp_idx++];
    q_clear();
    if (mode == M_READ_MULTI && cmd_len == 0) {
        q_read_block(multi_lba++);
        return resp[resp_idx++];
    }
    return 0xFF;
}

/* ---- pins ------------------------------------------------------------------- */

static void deselect(void)
{
    cs_active = 0;
    miso = 1;
    bitcnt = 0;
    in_shift = 0;
    cmd_len = 0;
    out_byte = 0xFF;
    q_clear();
    if (mode != M_CMD)
        mode = M_CMD;
}

void sd_pins(int ncs, int clk, int mosi)
{
    if (!present) {
        miso = 1;
        prev_clk = clk;
        return;
    }
    if (ncs) {
        if (cs_active)
            deselect();
        prev_clk = clk;
        return;
    }
    if (!cs_active) {
        cs_active = 1;
        bitcnt = 0;
        in_shift = 0;
        miso = (out_byte >> 7) & 1;
    }
    if (clk && !prev_clk) {
        in_shift = (in_shift << 1) | (mosi ? 1u : 0u);
        if (++bitcnt == 8) {
            bitcnt = 0;
            out_byte = exchange((uint8_t)in_shift);
            in_shift = 0;
        }
    } else if (!clk && prev_clk) {
        miso = (out_byte >> (7 - bitcnt)) & 1;
    }
    prev_clk = clk;
}

int sd_miso(void)
{
    return miso;
}

/* ---- lifecycle ---------------------------------------------------------------- */

int sd_init(const char *image_path, int do_trace)
{
    trace = do_trace;
    if (!image_path)
        return 0;
    fd = open(image_path, O_RDWR);
    if (fd < 0) {
        fd = open(image_path, O_RDONLY);
        if (fd >= 0)
            fprintf(stderr, "m68k-emu: sd: '%s' opened read-only\n", image_path);
    }
    if (fd < 0) {
        fprintf(stderr, "m68k-emu: sd: cannot open '%s': %s\n", image_path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (st.st_size % 512) != 0) {
        fprintf(stderr, "m68k-emu: sd: '%s' size must be a non-zero multiple of 512 bytes\n",
                image_path);
        close(fd);
        fd = -1;
        return -1;
    }
    nblocks = (uint32_t)(st.st_size / 512);
    present = 1;
    sd_reset();
    return 0;
}

void sd_shutdown(void)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
    present = 0;
}

void sd_reset(void)
{
    /* Card power is not cycled by the reset button; only the bus state. */
    deselect();
    prev_clk = 1;
}
