/* Motorola S-record parser. One line at a time; S1/S2/S3 data is handed to the store
 * callback, S7/S8/S9 supplies the entry point, S0/S4/S5/S6 are accepted and ignored. */
#include "bios.h"

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void srec_init(struct srec_state *s,
               void (*store)(void *, uint32_t, const uint8_t *, int), void *ctx)
{
    memset(s, 0, sizeof *s);
    s->store = store;
    s->ctx = ctx;
    s->min_addr = 0xFFFFFFFFu;
}

int srec_line(struct srec_state *s, const char *line)
{
    uint8_t buf[256];
    int count, i, addrlen, type;
    unsigned sum;

    while (*line == ' ' || *line == '\t') line++;
    if (line[0] != 'S' && line[0] != 's') { s->errors++; return -1; }
    type = hexval(line[1]);
    if (type < 0 || type == 4) { s->errors++; return -1; }
    switch (type) {
    case 0: case 1: case 5: case 9: addrlen = 2; break;
    case 2: case 6: case 8: addrlen = 3; break;
    case 3: case 7: addrlen = 4; break;
    default: s->errors++; return -1;
    }
    line += 2;
    {
        int h = hexval(line[0]), l = hexval(line[1]);
        if (h < 0 || l < 0) { s->errors++; return -1; }
        count = h * 16 + l;          /* bytes after the count: address + data + checksum */
    }
    line += 2;
    if (count < addrlen + 1) { s->errors++; return -1; }
    sum = (unsigned)count;
    for (i = 0; i < count; i++) {
        int h = hexval(line[0]), l = hexval(line[1]);
        if (h < 0 || l < 0) { s->errors++; return -1; }
        buf[i] = (uint8_t)(h * 16 + l);
        sum += buf[i];
        line += 2;
    }
    if ((sum & 0xFF) != 0xFF) { s->errors++; return -1; }
    while (*line == '\r' || *line == '\n' || *line == ' ') line++;
    if (*line) { s->errors++; return -1; }      /* trailing junk */

    {
        uint32_t addr = 0;
        int n = count - addrlen - 1;
        for (i = 0; i < addrlen; i++) addr = (addr << 8) | buf[i];
        if (type >= 1 && type <= 3) {
            if (n > 0) {
                if (s->store) s->store(s->ctx, addr, buf + addrlen, n);
                if (addr < s->min_addr) s->min_addr = addr;
                if (addr + (uint32_t)n - 1 > s->max_addr) s->max_addr = addr + (uint32_t)n - 1;
                s->bytes += (uint32_t)n;
                s->have_data = 1;
            }
            s->records++;
            return 0;
        }
        if (type >= 7) {
            s->entry = addr;
            s->have_entry = 1;
            s->done = 1;
            return 1;
        }
    }
    return 0;   /* S0 header, S5/S6 counts */
}
