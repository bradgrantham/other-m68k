/* SD card, native SD bus mode (SD Physical Layer Simplified Specification), behind the
 * SDIO bit-bang register.  Spec revision 7: six open-drain lines, CLK, CMD, DAT0..3.
 *
 * Timing (in CLK cycles, as the host produces them with SDIO writes):
 *   the card samples CMD and DAT on the rising edge and changes its outputs after the
 *   falling edge;
 *   NCR   = 8   clocks between a command's end bit and the response's start bit (spec 2..64)
 *   NAC   = 8   clocks between a response's end bit and a read block's start bit
 *   NCRC  = 2   clocks between a write block's end bit and the CRC status token on DAT0
 *   NBUSY = 16  clocks of DAT0 held low (programming) after the CRC status token
 *   a response or data transfer that is in progress is never interrupted except by CMD12.
 *
 * Commands: CMD0 (no response), CMD2, CMD3 (RCA = 0x1234), CMD7, CMD8 (R7), CMD9 (CSD),
 * CMD10 (CID), CMD12, CMD13, CMD16 (512 only), CMD17, CMD18, CMD24, CMD25, CMD55, ACMD6
 * (bus width 1 or 4), ACMD41 (busy on the first poll, ready on the second; an SDHC card
 * needs CMD8 first and HCS set, as real cards do), ACMD51 (SCR).  Anything else, or a
 * command in the wrong state, gets no response and sets ILLEGAL_COMMAND in the next
 * response; a command with a bad CRC7 gets no response and sets COM_CRC_ERROR.
 *
 * Default card: SDHC, block addressed, CSD v2 sized from the image.  --sd-sdsc: SDSC, byte
 * addressed (multiples of 512), CSD v1 (the image must be a multiple of 256 KB).
 *
 * Data: 1-bit on DAT0 until ACMD6 selects 4 bits (DAT3 = MSB of each nibble, high nibble of
 * a byte first).  Read blocks: start bit (0 on each active line), 512 bytes, CRC16-CCITT per
 * line over that line's data bits, end bit.  Write blocks: the same from the host; the card
 * checks every line's CRC, answers on DAT0 with start 0, "010" (accepted) or "101" (CRC
 * error), end 1, then holds DAT0 low for NBUSY clocks.  The block reaches the image only
 * when all CRCs are right and the address is in range.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hw.h"
#include "sdcard.h"

#define NCR   8
#define NAC   8
#define NCRC  2
#define NBUSY 16

enum { ST_IDLE = 0, ST_READY, ST_IDENT, ST_STBY, ST_TRAN, ST_DATA, ST_RCV, ST_PRG };

/* card status bits */
#define CS_OUT_OF_RANGE   (1u << 31)
#define CS_BLOCK_LEN_ERR  (1u << 29)
#define CS_COM_CRC_ERROR  (1u << 23)
#define CS_ILLEGAL_CMD    (1u << 22)
#define CS_ERROR          (1u << 19)
#define CS_READY_FOR_DATA (1u << 8)
#define CS_APP_CMD        (1u << 5)

static int fd = -1;
static int present, trace, sdsc;
static uint32_t nblocks;
static const unsigned rca = 0x1234;

/* bus */
static int prev_clk = 1;
static unsigned host_bits = 0x3F;
static unsigned card_cmd = 1;           /* card's CMD drive (1 = released) */
static unsigned card_dat = 0xF;         /* card's DAT3..0 drive */

/* card */
static int state;
static int bus4;                        /* 4-bit data bus selected */
static int seen_cmd8;
static int acmd41_polls;
static int app_cmd;                     /* next command is an ACMD */
static uint32_t sticky;                 /* error bits reported in the next response */
static unsigned long clk_count;

/* command receiver */
static int rx_active;
static uint64_t rx_shift;
static int rx_bits;

/* command transmitter */
static uint8_t tx_bits[136];
static int tx_len, tx_idx, tx_delay;

/* data transmitter: one symbol (DAT3..0 drive) per clock */
static uint8_t dtx[1 + 4096 + 16 + 1];
static int dtx_len, dtx_idx, dtx_delay;
static int dtx_armed;                   /* data may start: the response (if any) has ended */
static uint32_t read_lba;
static int read_multi;

/* data receiver */
enum { DRX_NONE = 0, DRX_WAIT_START, DRX_DATA, DRX_CRC, DRX_END };
static int drx;
static int drx_count;
static uint8_t wbuf[512];
static uint16_t crc_calc[4], crc_recv[4];
static uint32_t write_lba;
static int write_multi;

/* CRC status token and busy on DAT0 */
static uint8_t stx[1 + 3 + 1 + NBUSY];
static int stx_len, stx_idx, stx_delay;

/* ---- CRCs --------------------------------------------------------------------- */

static uint8_t crc7_bits(const uint8_t *bits, int n)
{
    uint8_t crc = 0;
    for (int i = 0; i < n; i++) {
        int x = ((crc >> 6) & 1) ^ (bits[i] & 1);
        crc = (uint8_t)((crc << 1) & 0x7F);
        if (x) crc ^= 0x09;
    }
    return crc;
}

static uint8_t crc7_bytes(const uint8_t *p, int n)
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

/* ---- registers ---------------------------------------------------------------- */

static void make_csd(uint8_t *csd)
{
    memset(csd, 0, 16);
    if (!sdsc) {
        uint32_t c_size = (nblocks + 1023) / 1024;      /* units of 512 KB */
        if (c_size) c_size--;
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
    } else {
        /* v1: capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN
         * with READ_BL_LEN = 9 and C_SIZE_MULT = 7: (C_SIZE + 1) * 512 blocks */
        uint32_t c_size = nblocks / 512;
        if (c_size) c_size--;
        if (c_size > 4095) c_size = 4095;
        csd[0] = 0x00;                              /* CSD_STRUCTURE = 0 (v1) */
        csd[1] = 0x26;
        csd[2] = 0x00;
        csd[3] = 0x32;
        csd[4] = 0x5F;
        csd[5] = 0x59;                              /* READ_BL_LEN = 9 */
        csd[6] = (uint8_t)(0x80 | ((c_size >> 10) & 0x03));  /* READ_BL_PARTIAL, C_SIZE[11:10] */
        csd[7] = (uint8_t)(c_size >> 2);
        csd[8] = (uint8_t)((c_size & 3) << 6 | 0x3F);
        csd[9] = 0xFF;
        csd[10] = (uint8_t)(0x80 | (7 << 2) | 1);   /* C_SIZE_MULT = 7, ERASE_BLK_EN */
        csd[11] = 0xFF;
        csd[12] = 0x0A;                             /* WRITE_BL_LEN = 9 */
        csd[13] = 0x40;
    }
    csd[14] = 0x00;
    csd[15] = (uint8_t)(crc7_bytes(csd, 15) << 1 | 1);
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
    cid[15] = (uint8_t)(crc7_bytes(cid, 15) << 1 | 1);
}

static void make_scr(uint8_t *scr)
{
    memset(scr, 0, 8);
    scr[0] = 0x02;                                  /* SCR v1, SD_SPEC 2.00 */
    scr[1] = 0x35;                                  /* data stat after erase 0, security 3, bus widths 1 and 4 */
}

static uint32_t ocr_value(int ready)
{
    uint32_t ocr = 0x00FF8000;                      /* 2.7-3.6 V */
    if (ready) {
        ocr |= 1u << 31;
        if (!sdsc) ocr |= 1u << 30;                 /* CCS */
    }
    return ocr;
}

/* ---- response construction ----------------------------------------------------- */

static void tx_begin(void) { tx_len = 0; }
static void tx_put(uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--)
        if (tx_len < (int)sizeof tx_bits)
            tx_bits[tx_len++] = (uint8_t)((v >> i) & 1);
}
static void tx_crc_end(void)
{
    uint8_t c = crc7_bits(tx_bits, tx_len);
    tx_put(c, 7);
    tx_put(1, 1);
}
static void tx_go(void)
{
    tx_idx = 0;
    tx_delay = NCR;
}

static uint32_t card_status(int app)
{
    uint32_t s = sticky | ((uint32_t)state << 9);
    if (state != ST_PRG) s |= CS_READY_FOR_DATA;
    if (app) s |= CS_APP_CMD;
    sticky = 0;
    return s;
}

static void resp_r1(unsigned cmd, uint32_t status)
{
    tx_begin();
    tx_put(0, 2); tx_put(cmd, 6); tx_put(status, 32); tx_crc_end(); tx_go();
}

static void resp_r2(const uint8_t *reg)
{
    tx_begin();
    tx_put(0, 2); tx_put(0x3F, 6);
    for (int i = 0; i < 16; i++) tx_put(reg[i], 8);  /* includes the register's CRC7 and end bit */
    tx_go();
}

static void resp_r3(uint32_t ocr)
{
    tx_begin();
    tx_put(0, 2); tx_put(0x3F, 6); tx_put(ocr, 32); tx_put(0x7F, 7); tx_put(1, 1); tx_go();
}

static void resp_r6(uint32_t status)
{
    uint32_t s16 = ((status >> 8) & 0xC000) | ((status >> 6) & 0x2000) | (status & 0x1FFF);
    tx_begin();
    tx_put(0, 2); tx_put(3, 6); tx_put(rca, 16); tx_put(s16, 16); tx_crc_end(); tx_go();
}

static void resp_r7(uint32_t arg)
{
    uint32_t v = (arg & 0xFFF);                     /* voltage accepted + check pattern echoed */
    tx_begin();
    tx_put(0, 2); tx_put(8, 6); tx_put(v, 32); tx_crc_end(); tx_go();
}

/* ---- data transmit ----------------------------------------------------------------- */

static void data_tx(const uint8_t *data, int n)
{
    uint16_t crc[4] = { 0, 0, 0, 0 };
    dtx_len = 0;
    if (bus4) {
        dtx[dtx_len++] = 0x0;                       /* start bit on every line */
        for (int i = 0; i < n; i++) {
            uint8_t hi = data[i] >> 4, lo = data[i] & 0xF;
            dtx[dtx_len++] = hi;
            dtx[dtx_len++] = lo;
            for (int l = 0; l < 4; l++) {
                crc[l] = crc16_bit(crc[l], (hi >> l) & 1);
                crc[l] = crc16_bit(crc[l], (lo >> l) & 1);
            }
        }
        for (int b = 15; b >= 0; b--) {
            uint8_t sym = 0;
            for (int l = 0; l < 4; l++) sym |= (uint8_t)(((crc[l] >> b) & 1) << l);
            dtx[dtx_len++] = sym;
        }
        dtx[dtx_len++] = 0xF;                       /* end bit */
    } else {
        dtx[dtx_len++] = 0xE;                       /* start bit on DAT0 */
        for (int i = 0; i < n; i++)
            for (int b = 7; b >= 0; b--) {
                int bit = (data[i] >> b) & 1;
                dtx[dtx_len++] = (uint8_t)(0xE | bit);
                crc[0] = crc16_bit(crc[0], bit);
            }
        for (int b = 15; b >= 0; b--)
            dtx[dtx_len++] = (uint8_t)(0xE | ((crc[0] >> b) & 1));
        dtx[dtx_len++] = 0xF;
    }
    dtx_idx = 0;
    dtx_delay = NAC;
    dtx_armed = (tx_len == 0);          /* otherwise armed when the response's end bit has gone */
}

static void send_block(uint32_t lba)
{
    uint8_t buf[512];
    if (pread(fd, buf, sizeof buf, (off_t)lba * 512) != (ssize_t)sizeof buf) {
        fprintf(stderr, "m68k-emu: sd: read of block %u failed\n", lba);
        memset(buf, 0, sizeof buf);
    }
    if (trace) fprintf(stderr, "m68k-emu: sd: read block %u (%d-bit)\n", lba, bus4 ? 4 : 1);
    data_tx(buf, 512);
}

/* ---- data receive (writes) ----------------------------------------------------------- */

static void drx_begin(void)
{
    drx = DRX_WAIT_START;
}

static void status_token(int ok)
{
    stx_len = 0;
    stx[stx_len++] = 0xE;                           /* start bit */
    stx[stx_len++] = (uint8_t)(0xE | (ok ? 0 : 1)); /* 010 accepted, 101 CRC error */
    stx[stx_len++] = (uint8_t)(0xE | (ok ? 1 : 0));
    stx[stx_len++] = (uint8_t)(0xE | (ok ? 0 : 1));
    stx[stx_len++] = 0xF;                           /* end bit */
    for (int i = 0; i < NBUSY; i++) stx[stx_len++] = 0xE;   /* busy */
    stx_idx = 0;
    stx_delay = NCRC;
    state = ST_PRG;
}

static void finish_write(void)
{
    int ok = 1;
    int lines = bus4 ? 4 : 1;
    for (int l = 0; l < lines; l++)
        if (crc_calc[l] != crc_recv[l]) ok = 0;
    if (ok) {
        if (write_lba >= nblocks) {
            ok = 0;                                 /* cannot happen: checked at the command */
        } else if (pwrite(fd, wbuf, 512, (off_t)write_lba * 512) != 512) {
            fprintf(stderr, "m68k-emu: sd: write of block %u failed: %s\n", write_lba, strerror(errno));
            ok = 0;
        }
    }
    if (trace) fprintf(stderr, "m68k-emu: sd: %s block %u (%d-bit, CRC %s)\n",
                       ok ? "wrote" : "rejected", write_lba, lines, ok ? "ok" : "bad");
    status_token(ok);
    write_lba++;
}

/* ---- commands ---------------------------------------------------------------------- */

static int rca_match(uint32_t arg)
{
    return (arg >> 16) == rca;
}

static void illegal(unsigned cmd, int app)
{
    sticky |= CS_ILLEGAL_CMD;
    if (trace) fprintf(stderr, "m68k-emu: sd: %sCMD%u illegal in state %d (no response)\n",
                       app ? "A" : "", cmd, state);
}

static void handle_command(uint64_t frame)
{
    unsigned cmd = (unsigned)((frame >> 40) & 0x3F);
    uint32_t arg = (uint32_t)(frame >> 8);
    int app = app_cmd;
    app_cmd = 0;
    int st_before = state;
    uint32_t status;

    if (app) {
        switch (cmd) {
        case 41:                                    /* SD_SEND_OP_COND */
            if (state != ST_IDLE) { illegal(cmd, 1); return; }
            acmd41_polls++;
            {
                int can_ready = sdsc || (seen_cmd8 && (arg & (1u << 30)));
                int ready = acmd41_polls >= 2 && can_ready;
                if (ready) state = ST_READY;
                if (trace) fprintf(stderr, "m68k-emu: sd: ACMD41 arg=%08X -> R3 OCR=%08X%s\n", arg,
                                   ocr_value(ready),
                                   (!ready && acmd41_polls >= 10) ? " (SDHC needs CMD8 and HCS to leave busy)" : "");
                resp_r3(ocr_value(ready));
            }
            return;
        case 6:                                     /* SET_BUS_WIDTH */
            if (state != ST_TRAN) { illegal(cmd, 1); return; }
            status = card_status(1);
            if ((arg & 3) == 2) bus4 = 1; else if ((arg & 3) == 0) bus4 = 0; else status |= CS_ERROR;
            if (trace) fprintf(stderr, "m68k-emu: sd: ACMD6 arg=%08X -> %d-bit bus\n", arg, bus4 ? 4 : 1);
            resp_r1(cmd, status);
            return;
        case 51: {                                  /* SEND_SCR */
            if (state != ST_TRAN) { illegal(cmd, 1); return; }
            uint8_t scr[8];
            make_scr(scr);
            resp_r1(cmd, card_status(1));
            if (trace) fprintf(stderr, "m68k-emu: sd: ACMD51 -> SCR\n");
            data_tx(scr, 8);
            state = ST_DATA;
            read_multi = 0;
            return;
        }
        default:
            /* an unknown ACMD is treated as the plain command (spec 4.3.9.1) */
            break;
        }
    }

    switch (cmd) {
    case 0:                                         /* GO_IDLE_STATE: no response */
        state = ST_IDLE; bus4 = 0; seen_cmd8 = 0; acmd41_polls = 0; sticky = 0;
        dtx_len = 0; drx = DRX_NONE; stx_len = 0;
        if (trace) fprintf(stderr, "m68k-emu: sd: CMD0 (idle, no response)\n");
        return;
    case 8:                                         /* SEND_IF_COND */
        if (state != ST_IDLE) { illegal(cmd, app); return; }
        if (((arg >> 8) & 0xF) != 1) {
            if (trace) fprintf(stderr, "m68k-emu: sd: CMD8 arg=%08X voltage not supported (no response)\n", arg);
            return;
        }
        seen_cmd8 = 1;
        resp_r7(arg);
        break;
    case 55:                                        /* APP_CMD */
        app_cmd = 1;
        resp_r1(cmd, card_status(1));
        break;
    case 2:                                         /* ALL_SEND_CID */
        if (state != ST_READY) { illegal(cmd, app); return; }
        { uint8_t cid[16]; make_cid(cid); resp_r2(cid); }
        state = ST_IDENT;
        break;
    case 3:                                         /* SEND_RELATIVE_ADDR */
        if (state != ST_IDENT && state != ST_STBY) { illegal(cmd, app); return; }
        state = ST_STBY;
        resp_r6(card_status(app));
        break;
    case 7:                                         /* SELECT/DESELECT_CARD */
        if (rca_match(arg)) {
            if (state == ST_STBY) { status = card_status(app); state = ST_TRAN; resp_r1(cmd, status); }
            else if (state == ST_TRAN || state == ST_DATA) { resp_r1(cmd, card_status(app)); }
            else { illegal(cmd, app); return; }
        } else {
            if (state == ST_TRAN || state == ST_DATA || state == ST_STBY) { state = ST_STBY; dtx_len = 0; }
            if (trace) fprintf(stderr, "m68k-emu: sd: CMD7 arg=%08X deselect (no response)\n", arg);
            return;
        }
        break;
    case 9: case 10:                                /* SEND_CSD / SEND_CID */
        if (state != ST_STBY || !rca_match(arg)) { illegal(cmd, app); return; }
        { uint8_t reg[16]; if (cmd == 9) make_csd(reg); else make_cid(reg); resp_r2(reg); }
        break;
    case 12:                                        /* STOP_TRANSMISSION */
        if (state != ST_DATA && state != ST_RCV && state != ST_TRAN) { illegal(cmd, app); return; }
        dtx_len = 0; drx = DRX_NONE; read_multi = 0; write_multi = 0;
        status = card_status(app);
        state = ST_TRAN;
        resp_r1(cmd, status);
        break;
    case 13:                                        /* SEND_STATUS */
        if (state < ST_STBY || !rca_match(arg)) { illegal(cmd, app); return; }
        resp_r1(cmd, card_status(app));
        break;
    case 16:                                        /* SET_BLOCKLEN */
        if (state != ST_TRAN) { illegal(cmd, app); return; }
        status = card_status(app);
        if (arg != 512) status |= CS_BLOCK_LEN_ERR;
        resp_r1(cmd, status);
        break;
    case 17: case 18: {                             /* READ_SINGLE/MULTIPLE_BLOCK */
        if (state != ST_TRAN) { illegal(cmd, app); return; }
        uint32_t lba = sdsc ? arg / 512 : arg;
        status = card_status(app);
        if ((sdsc && (arg & 511)) || lba >= nblocks) {
            resp_r1(cmd, status | CS_OUT_OF_RANGE);
            if (trace) fprintf(stderr, "m68k-emu: sd: CMD%u arg=%08X out of range\n", cmd, arg);
            return;
        }
        resp_r1(cmd, status);
        read_lba = lba;
        read_multi = (cmd == 18);
        state = ST_DATA;
        if (trace) fprintf(stderr, "m68k-emu: sd: %sCMD%u arg=%08X state %d->%d\n", app ? "A" : "", cmd, arg, st_before, state);
        send_block(read_lba);
        return;
    }
    case 24: case 25: {                             /* WRITE_BLOCK / WRITE_MULTIPLE_BLOCK */
        if (state != ST_TRAN) { illegal(cmd, app); return; }
        uint32_t lba = sdsc ? arg / 512 : arg;
        status = card_status(app);
        if ((sdsc && (arg & 511)) || lba >= nblocks) {
            resp_r1(cmd, status | CS_OUT_OF_RANGE);
            if (trace) fprintf(stderr, "m68k-emu: sd: CMD%u arg=%08X out of range\n", cmd, arg);
            return;
        }
        resp_r1(cmd, status);
        write_lba = lba;
        write_multi = (cmd == 25);
        state = ST_RCV;
        drx_begin();
        break;
    }
    default:
        illegal(cmd, app);
        return;
    }
    if (trace && cmd != 55)
        fprintf(stderr, "m68k-emu: sd: %sCMD%u arg=%08X state %d->%d\n", app ? "A" : "", cmd, arg, st_before, state);
    else if (trace)
        fprintf(stderr, "m68k-emu: sd: CMD55 (APP_CMD)\n");
}

static void command_received(void)
{
    uint64_t frame = rx_shift;                      /* 48 bits: start 0 (bit 47) .. end bit (bit 0) */
    if (!((frame >> 46) & 1))                       /* transmission bit must be 1 (host -> card) */
        return;
    uint8_t bits[40];
    for (int i = 0; i < 40; i++) bits[i] = (uint8_t)((frame >> (47 - i)) & 1);
    uint8_t crc = crc7_bits(bits, 40);
    if (crc != ((frame >> 1) & 0x7F) || !(frame & 1)) {
        sticky |= CS_COM_CRC_ERROR;
        if (trace) fprintf(stderr, "m68k-emu: sd: CMD%u bad CRC7 (%02X, expected %02X): no response\n",
                           (unsigned)((frame >> 40) & 0x3F), (unsigned)((frame >> 1) & 0x7F), crc);
        return;
    }
    handle_command(frame);
}

/* ---- clock edges ---------------------------------------------------------------------- */

static void rising_edge(void)
{
    unsigned cmd_level = ((host_bits >> SDIO_CMD_BIT) & 1) & card_cmd;
    unsigned dat_level = (((host_bits >> SDIO_DAT0_BIT) & 1) |
                          ((host_bits >> SDIO_DAT1_BIT) & 1) << 1 |
                          ((host_bits >> SDIO_DAT2_BIT) & 1) << 2 |
                          ((host_bits >> SDIO_DAT3_BIT) & 1) << 3) & card_dat;
    clk_count++;

    /* command receiver: only while the card is not answering */
    if (tx_len == 0) {
        if (!rx_active) {
            if (cmd_level == 0) { rx_active = 1; rx_shift = 0; rx_bits = 1; }
        } else {
            rx_shift = (rx_shift << 1) | cmd_level;
            if (++rx_bits == 48) {
                rx_active = 0;
                command_received();
            }
        }
    }

    /* data receiver (write block) */
    switch (drx) {
    case DRX_WAIT_START:
        if ((dat_level & 1) == 0) {
            drx = DRX_DATA; drx_count = 0;
            memset(crc_calc, 0, sizeof crc_calc); memset(crc_recv, 0, sizeof crc_recv);
            memset(wbuf, 0, sizeof wbuf);
        }
        break;
    case DRX_DATA:
        if (bus4) {
            wbuf[drx_count >> 1] |= (uint8_t)((drx_count & 1) ? (dat_level & 0xF) : (dat_level & 0xF) << 4);
            for (int l = 0; l < 4; l++) crc_calc[l] = crc16_bit(crc_calc[l], (dat_level >> l) & 1);
            if (++drx_count == 1024) { drx = DRX_CRC; drx_count = 0; }
        } else {
            wbuf[drx_count >> 3] |= (uint8_t)((dat_level & 1) << (7 - (drx_count & 7)));
            crc_calc[0] = crc16_bit(crc_calc[0], dat_level & 1);
            if (++drx_count == 4096) { drx = DRX_CRC; drx_count = 0; }
        }
        break;
    case DRX_CRC:
        for (int l = 0; l < 4; l++) crc_recv[l] = (uint16_t)((crc_recv[l] << 1) | ((dat_level >> l) & 1));
        if (++drx_count == 16) drx = DRX_END;
        break;
    case DRX_END:
        drx = DRX_NONE;
        finish_write();
        break;
    default:
        break;
    }
}

static void falling_edge(void)
{
    /* CMD */
    if (tx_len) {
        if (tx_delay > 0) { tx_delay--; card_cmd = 1; }
        else {
            card_cmd = tx_bits[tx_idx++];
            if (tx_idx == tx_len) {
                tx_len = 0;
                if (dtx_len) dtx_armed = 1;     /* Nac counts from the end of the response */
            }
        }
    } else {
        card_cmd = 1;
    }

    /* DAT */
    if (dtx_len && dtx_armed) {
        if (dtx_delay > 0) { dtx_delay--; card_dat = 0xF; }
        else {
            card_dat = dtx[dtx_idx++];
            if (dtx_idx == dtx_len) {
                dtx_len = 0;
                if (read_multi && state == ST_DATA) {
                    read_lba++;
                    if (read_lba < nblocks) send_block(read_lba);
                    else { read_multi = 0; state = ST_TRAN; sticky |= CS_OUT_OF_RANGE; }
                } else if (state == ST_DATA) {
                    state = ST_TRAN;
                }
            }
        }
    } else if (stx_len && !dtx_len) {
        if (stx_delay > 0) { stx_delay--; card_dat = 0xF; }
        else {
            card_dat = stx[stx_idx++];
            if (stx_idx == stx_len) {
                stx_len = 0;
                if (write_multi) { state = ST_RCV; drx_begin(); }
                else state = ST_TRAN;
            }
        }
    } else {
        card_dat = 0xF;
    }
}

void sd_bus(unsigned host_drive)
{
    host_bits = host_drive & 0x3F;
    int clk = (host_bits >> SDIO_CLK_BIT) & 1;
    if (!present) { prev_clk = clk; return; }
    if (clk && !prev_clk) rising_edge();
    else if (!clk && prev_clk) falling_edge();
    prev_clk = clk;
}

unsigned sd_lines(void)
{
    if (!present) return 0x3F;
    unsigned v = (1u << SDIO_CLK_BIT);
    if (card_cmd) v |= 1u << SDIO_CMD_BIT;
    if (card_dat & 1) v |= 1u << SDIO_DAT0_BIT;
    if (card_dat & 2) v |= 1u << SDIO_DAT1_BIT;
    if (card_dat & 4) v |= 1u << SDIO_DAT2_BIT;
    if (card_dat & 8) v |= 1u << SDIO_DAT3_BIT;
    return v;
}

/* ---- lifecycle ---------------------------------------------------------------------- */

int sd_init(const char *image_path, int do_trace, int do_sdsc)
{
    trace = do_trace;
    sdsc = do_sdsc;
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
        fprintf(stderr, "m68k-emu: sd: '%s' size must be a non-zero multiple of 512 bytes\n", image_path);
        close(fd); fd = -1;
        return -1;
    }
    if (sdsc && (st.st_size % (256 * 1024)) != 0) {
        fprintf(stderr, "m68k-emu: sd: an SDSC image must be a multiple of 256 KB\n");
        close(fd); fd = -1;
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
    /* Card power is not cycled by the reset button: only the bus state is reset.  The
     * firmware must be able to re-initialise a card that is already past idle, which is
     * what CMD0 is for. */
    prev_clk = 1;
    host_bits = 0x3F;
    card_cmd = 1; card_dat = 0xF;
    rx_active = 0; rx_bits = 0;
    tx_len = 0; dtx_len = 0; dtx_armed = 0; stx_len = 0; drx = DRX_NONE;
    app_cmd = 0; sticky = 0;
}
