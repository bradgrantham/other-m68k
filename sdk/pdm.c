/* pdm.c: first-order sigma-delta encoder for the AUDIO shifter. See pdm.h. */
#include "pdm.h"

uint16_t pdm_table[32768];
static uint16_t pdm_accw;                 /* accumulator << 8, so (accw | sample) >> 1 is the index */

/* Entry (acc, v7): eight steps of a += 2 * v7 with the carry out of each shifted into the
 * pattern. The 68000 does this in two instructions per step, add.b (sets X) and addx.b
 * pat,pat (pat = 2 * pat + X), 8 clocks a step, so the whole 32768-entry table takes about
 * 0.4 s at 12 MHz. */
static inline uint16_t pdm_entry(unsigned acc, unsigned inc)
{
    unsigned a = acc, p = 0;
    __asm__(
        ".rept 8\n\t"
        "add.b %[inc],%[a]\n\t"
        "addx.b %[p],%[p]\n\t"
        ".endr"
        : [a] "+d" (a), [p] "+d" (p)
        : [inc] "d" (inc)
        : "cc");
    return (uint16_t)(((a & 255) << 8) | (p & 255));
}

void pdm_init(void)
{
    uint16_t *t = pdm_table;
    for (unsigned acc = 0; acc < 256; acc++)
        for (unsigned inc = 0; inc < 256; inc += 2)        /* inc = 2 * v7 */
            *t++ = pdm_entry(acc, inc);
    pdm_accw = 0;
}

void pdm_reset(void)
{
    pdm_accw = 0;
}

void pdm_encode(uint8_t *dst, const uint8_t *samples, unsigned n)
{
    uint16_t accw = pdm_accw;
    while (n--) {
        uint16_t e = pdm_table[(uint16_t)(accw | *samples++) >> 1];
        *dst++ = (uint8_t)e;
        accw = e & 0xFF00;
    }
    pdm_accw = accw;
}

uint8_t pdm_silence(void)
{
    return 0xAA;
}
