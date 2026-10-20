/* SD command framing: sd.c drives a fake SPI card that checks start bits, CRC7 and
 * arguments, records the command sequence and answers like an SDHC (or SDSC v1) card. */
#include "test.h"
#include "sd.h"

/* ---- fake card ---------------------------------------------------------------------- */

enum { CARD_NONE, CARD_SDHC, CARD_SDSC_V1 };
static int card_kind = CARD_SDHC;
static int selected, slow_mode;
static int cmd_log[64], cmd_args_log[64], ncmds;
static int bad_crc, bad_start, idle_clocks_before_cmd0, clocks_with_cs_high, ever_selected;
static int acmd_pending, is_idle = 1, acmd41_polls;
static uint8_t cmdbuf[6]; static int cmdlen;
static uint8_t resp[1100]; static int resp_len, resp_idx;
static int write_state, wlen; static uint8_t wbuf[514]; static uint32_t write_addr;
static uint8_t block_image[2][512];
static int slow_during_cmd41 = 1, fast_during_read;

static void q(uint8_t b) { if (resp_len < (int)sizeof resp) resp[resp_len++] = b; }
static void q_block(const uint8_t *d, int n) { q(0xFF); q(0xFE); for (int i = 0; i < n; i++) q(d[i]); q(0xAA); q(0x55); }

static void handle_cmd(void)
{
    unsigned cmd = cmdbuf[0] & 0x3F;
    uint32_t arg = ((uint32_t)cmdbuf[1] << 24) | ((uint32_t)cmdbuf[2] << 16) | ((uint32_t)cmdbuf[3] << 8) | cmdbuf[4];
    if ((cmdbuf[0] & 0xC0) != 0x40) bad_start++;
    if (sd_crc7(cmdbuf, 5) != cmdbuf[5]) bad_crc++;
    if (ncmds < 64) { cmd_log[ncmds] = (int)cmd | (acmd_pending ? 0x100 : 0); cmd_args_log[ncmds] = (int)arg; ncmds++; }
    int was_acmd = acmd_pending;
    acmd_pending = 0;
    uint8_t r1 = is_idle ? 0x01 : 0x00;
    resp_len = resp_idx = 0;
    q(0xFF);                                            /* Ncr */
    if (was_acmd && cmd == 41) {
        if (!slow_mode) slow_during_cmd41 = 0;
        if (++acmd41_polls >= 3) is_idle = 0;
        q(is_idle ? 0x01 : 0x00);
        return;
    }
    switch (cmd) {
    case 0: is_idle = 1; acmd41_polls = 0; q(0x01); break;
    case 8:
        if (card_kind == CARD_SDSC_V1) { q(0x05); break; }  /* illegal command */
        q(0x01); q(0); q(0); q(0x01); q((uint8_t)arg); break;
    case 55: acmd_pending = 1; q(r1); break;
    case 58: q(r1); q(card_kind == CARD_SDHC ? 0xC0 : 0x80); q(0xFF); q(0x80); q(0); break;
    case 16: q(arg == 512 ? r1 : (uint8_t)(r1 | 0x40)); break;
    case 9: {
        uint8_t csd[16] = {0};
        if (card_kind == CARD_SDHC) { csd[0] = 0x40; csd[7] = 0; csd[8] = 0x00; csd[9] = 0x7F; }   /* C_SIZE 127 -> 64 MB */
        else { csd[0] = 0x00; csd[5] = 0x09; csd[6] = 0x00; csd[7] = 0x7F; csd[8] = 0xC0; csd[9] = 0x03; csd[10] = 0x80; } /* C_SIZE 511, MULT 7 */
        csd[15] = sd_crc7(csd, 15);
        q(r1); q_block(csd, 16); break;
    }
    case 10: {
        uint8_t cid[16] = { 0x01, 'F', 'K', 'T', 'E', 'S', 'T', '0', 0x10, 1, 2, 3, 4, 0x01, 0x8A, 0 };
        cid[15] = sd_crc7(cid, 15);
        q(r1); q_block(cid, 16); break;
    }
    case 17:
        if (slow_mode) fast_during_read = -1;
        if (card_kind == CARD_SDHC ? arg >= 2 : arg >= 1024) { q(r1); q(0xFF); q(0x08); break; }
        q(r1); q_block(block_image[card_kind == CARD_SDHC ? arg : arg / 512], 512); break;
    case 24:
        if (card_kind == CARD_SDHC ? arg >= 2 : arg >= 1024) { q((uint8_t)(r1 | 0x40)); break; }
        q(r1); write_state = 1; wlen = 0; write_addr = card_kind == CARD_SDHC ? arg : arg / 512; break;
    default: q((uint8_t)(r1 | 0x04)); break;
    }
}

uint8_t sdhw_xfer(uint8_t in)
{
    if (!selected) {
        clocks_with_cs_high += 8;
        if (ncmds == 0) idle_clocks_before_cmd0 += 8;
        return 0xFF;
    }
    if (card_kind == CARD_NONE) return 0xFF;
    if (write_state == 1) {                             /* waiting for the start token */
        if (in == 0xFE) { write_state = 2; wlen = 0; }
        return resp_idx < resp_len ? resp[resp_idx++] : 0xFF;
    }
    if (write_state == 2) {
        wbuf[wlen++] = in;
        if (wlen == 514) {
            memcpy(block_image[write_addr], wbuf, 512);
            write_state = 0;
            resp_len = resp_idx = 0;
            q(0x05); q(0x00); q(0x00); q(0x00);        /* accepted, busy for 3 bytes */
        }
        return 0xFF;
    }
    if (cmdlen == 0) {
        if ((in & 0xC0) == 0x40) cmdbuf[cmdlen++] = in;
    } else {
        cmdbuf[cmdlen++] = in;
        if (cmdlen == 6) { cmdlen = 0; handle_cmd(); }
    }
    if (resp_idx < resp_len) return resp[resp_idx++];
    return 0xFF;
}

void sdhw_select(int on)
{
    if (on) ever_selected = 1;
    if (!on) { cmdlen = 0; resp_len = resp_idx = 0; }
    selected = on;
}

void sdhw_slow(int on) { slow_mode = on; }
void sdhw_read(uint8_t *buf, int n) { while (n--) *buf++ = sdhw_xfer(0xFF); }
void sd_hw_init(void) { sdio_shadow = SDIO_CS; }

static void card_reset(int kind)
{
    card_kind = kind;
    selected = slow_mode = 0; ncmds = 0; bad_crc = bad_start = 0; idle_clocks_before_cmd0 = 0;
    clocks_with_cs_high = 0; ever_selected = 0; acmd_pending = 0; is_idle = 1; acmd41_polls = 0;
    cmdlen = 0; resp_len = resp_idx = 0; write_state = 0; slow_during_cmd41 = 1; fast_during_read = 0;
    for (int i = 0; i < 512; i++) { block_image[0][i] = (uint8_t)i; block_image[1][i] = (uint8_t)(255 - i); }
}

static int count_cmd(int c)
{
    int n = 0;
    for (int i = 0; i < ncmds; i++) if (cmd_log[i] == c) n++;
    return n;
}

/* ---- tests ---------------------------------------------------------------------------- */

void test_sd(void)
{
    uint8_t buf[512];

    /* CRC7 known values: CMD0 arg 0 -> 0x95, CMD8 arg 0x1AA -> 0x87 */
    uint8_t c0[5] = { 0x40, 0, 0, 0, 0 }, c8[5] = { 0x48, 0, 0, 0x01, 0xAA };
    CHECK_EQ(sd_crc7(c0, 5), 0x95);
    CHECK_EQ(sd_crc7(c8, 5), 0x87);

    /* SDHC: full init */
    card_reset(CARD_SDHC);
    CHECK_EQ(sd_init(), SD_OK);
    CHECK(sd_card.ready);
    CHECK_EQ(sd_card.type, SD_V2_HC);
    CHECK_EQ(sd_card.block_addr, 1);
    CHECK_EQ(sd_card.blocks, 128 * 1024);                   /* (127+1) * 1024 blocks = 64 MB */
    CHECK(idle_clocks_before_cmd0 >= 74);
    CHECK_EQ(bad_crc, 0);
    CHECK_EQ(bad_start, 0);
    CHECK_EQ(cmd_log[0], 0);                                /* CMD0 first */
    CHECK_EQ(cmd_log[1], 8); CHECK_EQ(cmd_args_log[1], 0x1AA);
    CHECK_EQ(cmd_log[2], 55); CHECK_EQ(cmd_log[3], 0x129); CHECK(cmd_args_log[3] & 0x40000000);  /* ACMD41 with HCS */
    CHECK_EQ(count_cmd(0x129), 3);                          /* polled until ready */
    CHECK_EQ(count_cmd(58), 1);
    CHECK_EQ(count_cmd(16), 0);                             /* block addressed: no CMD16 */
    CHECK_EQ(count_cmd(9), 1); CHECK_EQ(count_cmd(10), 1);
    CHECK(slow_during_cmd41);                               /* still in slow mode through ACMD41 */
    CHECK_EQ(sd_card.cid[1], 'F');
    CHECK_EQ(sd_card.ocr[0], 0xC0);
    CHECK_STR(sd_type_name(sd_card.type), "SDHC/SDXC (block addressed)");

    /* read: block 1 pattern, CMD17 with the block number */
    ncmds = 0;
    CHECK_EQ(sd_read_block(1, buf), SD_OK);
    CHECK_EQ(cmd_log[0], 17); CHECK_EQ(cmd_args_log[0], 1);
    CHECK_EQ(buf[0], 255); CHECK_EQ(buf[511], 0);
    CHECK_EQ(fast_during_read, 0);                          /* full speed after init */
    /* out of range: our own check (CSD) first */
    CHECK_EQ(sd_read_block(128 * 1024, buf), SD_ERR_RANGE);
    /* card-reported range error (data error token) */
    sd_card.blocks = 0;
    CHECK_EQ(sd_read_block(5, buf), SD_ERR_TOKEN);
    sd_card.blocks = 128 * 1024;

    /* write then read back */
    for (int i = 0; i < 512; i++) buf[i] = (uint8_t)(i * 7);
    ncmds = 0;
    CHECK_EQ(sd_write_block(0, buf), SD_OK);
    CHECK_EQ(cmd_log[0], 24); CHECK_EQ(cmd_args_log[0], 0);
    CHECK_EQ(block_image[0][3], 21);
    memset(buf, 0, sizeof buf);
    CHECK_EQ(sd_read_block(0, buf), SD_OK);
    CHECK_EQ(buf[3], 21); CHECK_EQ(buf[511], (uint8_t)(511 * 7));
    CHECK_EQ(sd_write_block(7, buf), SD_ERR_CMD);          /* card says address error */
    CHECK_EQ(bad_crc, 0);
    CHECK(!selected);                                       /* deselected after every op */

    /* SDSC v1: CMD8 rejected, no HCS, CMD16 512, byte addressing */
    card_reset(CARD_SDSC_V1);
    CHECK_EQ(sd_init(), SD_OK);
    CHECK_EQ(sd_card.type, SD_V1);
    CHECK_EQ(sd_card.block_addr, 0);
    CHECK_EQ(count_cmd(16), 1);
    CHECK_EQ(count_cmd(58), 0);
    CHECK_EQ(cmd_args_log[3] & 0x40000000, 0);             /* ACMD41 without HCS */
    CHECK_EQ(sd_card.blocks, 262144);                        /* (511+1) << (7+2) blocks of 2^9 bytes = 128 MB */
    ncmds = 0;
    CHECK_EQ(sd_read_block(1, buf), SD_OK);
    CHECK_EQ(cmd_args_log[0], 512);                         /* byte address */
    CHECK_EQ(buf[0], 255);

    /* no card: CMD0 never answers */
    card_reset(CARD_NONE);
    CHECK_EQ(sd_init(), SD_ERR_NOCARD);
    CHECK(!sd_card.ready);
    CHECK_EQ(sd_read_block(0, buf), SD_ERR_NOINIT);
    CHECK_STR(sd_strerror(SD_ERR_NOCARD), "no card");

    /* CSD v1 capacity formula: 2 GB card (C_SIZE 4095, MULT 7, BL_LEN 10) */
    uint8_t csd[16] = {0};
    csd[5] = 0x0A; csd[6] = 0x03; csd[7] = 0xFF; csd[8] = 0xC0; csd[9] = 0x03; csd[10] = 0x80;
    CHECK_EQ(sd_csd_blocks(csd), 4096u * 512 * 2);
}
