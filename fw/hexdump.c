/* One line of a memory dump, 8 bytes wide so it fits the 40-column console:
 * "004000 00 11 22 33 44 55 66 77 ........" (39 chars + NUL). */
#include "bios.h"

static const char hexd[] = "0123456789abcdef";

void hexdump_line(char *out, uint32_t addr, const uint8_t *data, int n)
{
    char *p = out;
    int i;
    for (i = 20; i >= 0; i -= 4)
        *p++ = hexd[(addr >> i) & 15];
    for (i = 0; i < HEXDUMP_BYTES_PER_LINE; i++) {
        *p++ = ' ';
        if (i < n) { *p++ = hexd[data[i] >> 4]; *p++ = hexd[data[i] & 15]; }
        else { *p++ = ' '; *p++ = ' '; }
    }
    *p++ = ' ';
    for (i = 0; i < n; i++)
        *p++ = (data[i] >= 0x20 && data[i] < 0x7F) ? (char)data[i] : '.';
    *p = 0;
}
