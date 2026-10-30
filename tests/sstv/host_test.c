/* host_test.c: the SSTV encoder on the host. Draws a test card, checks the sample budget of
 * every mode at the NTSC, PAL and 48 kHz rates (total length right to a sample, the same
 * stream whatever the chunk size, 1900 Hz lead-in), and writes card.ppm plus one 16-bit
 * WAV per mode and rate for decode.py.  Usage: host_test OUTDIR */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sstv_enc.c"
#include "font6x10.h"

static int checks, fails;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint8_t card[240][320];

static void text(int x0, int y0, const char *s, uint8_t fg, int scale)
{
    for (; *s; s++, x0 += 6 * scale)
        for (int r = 0; r < 10; r++)
            for (int c = 0; c < 6; c++)
                if (font6x10[(unsigned char)*s][r] >> (5 - c) & 1)
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++) {
                            int x = x0 + c * scale + sx, y = y0 + r * scale + sy;
                            if (x >= 0 && x < 320 && y >= 0 && y < 240) card[y][x] = fg;
                        }
}

static void draw_card(void)
{
    static const uint8_t bars[8] = { 0xFF, 0xFC, 0x1F, 0x1C, 0xE3, 0xE0, 0x03, 0x00 };   /* W Y C G M R B K */
    for (int y = 0; y < 160; y++)
        for (int x = 0; x < 320; x++) card[y][x] = bars[x / 40];
    for (int y = 160; y < 200; y++)
        for (int x = 0; x < 320; x++) { int v = x * 255 / 319; card[y][x] = (uint8_t)(((v >> 5) << 5) | ((v >> 5) << 2) | (v >> 6)); }
    for (int y = 200; y < 240; y++)
        for (int x = 0; x < 320; x++) card[y][x] = 0x02;                                    /* dark blue */
    text(8, 204, "griffin-jr SSTV", 0xFF, 2);
    text(8, 226, "test card: bars, ramp, text", 0xFF, 1);
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n320 240\n255\n");
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            int c = card[y][x];
            uint8_t px[3] = { (uint8_t)(((c >> 5) * 255 + 3) / 7), (uint8_t)((((c >> 2) & 7) * 255 + 3) / 7), (uint8_t)((c & 3) * 85) };
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 }; fwrite(b, 1, 4, f); }
static void put16(FILE *f, uint32_t v) { uint8_t b[2] = { v, v >> 8 }; fwrite(b, 1, 2, f); }

static void write_wav(const char *path, const uint8_t *pcm, uint32_t n, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n * 2); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, n * 2);
    for (uint32_t i = 0; i < n; i++) put16(f, (uint16_t)(int16_t)(((int)pcm[i] - 128) * 256));
    fclose(f);
}

static uint32_t fnv(const uint8_t *p, uint32_t n) { uint32_t h = 2166136261u; while (n--) h = (h ^ *p++) * 16777619u; return h; }

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    char path[512];
    draw_card();
    snprintf(path, sizeof path, "%s/card.ppm", out); write_ppm(path);

    static const struct { const char *name; uint32_t num, den; } rates[] = {
        { "ntsc", 12000000, 764 }, { "pal", 12000000, 768 }, { "48k", 48000, 1 } };
    static const char *mnames[3] = { "r36", "r72", "m1" };
    static sstv_enc_t e;
    for (int m = 0; m < 3; m++) {
        for (int r = 0; r < 3; r++) {
            sstv_mode_t mode = (sstv_mode_t)m;
            CHECK(sstv_encoder_init(&e, &card[0][0], 320, 240, 320, mode, rates[r].num, rates[r].den) == 0);
            uint32_t total = sstv_encoder_total_samples(&e);
            /* analytic length: lead 500 + VIS 910 + lines + trailer 500 ms */
            uint64_t us = 500000 + 910000 + (uint64_t)sstv_mode_lines(mode) * sstv_mode_line_us(mode) + 500000;
            double expect = (double)us * rates[r].num / ((double)rates[r].den * 1e6);
            CHECK(total > expect - 1.5 && total < expect + 1.5);
            uint8_t *pcm = malloc(total + 1024);
            uint32_t n = 0, got;
            while ((got = (uint32_t)sstv_encoder_next_pcm(&e, pcm + n, 333)) > 0) n += got;
            CHECK(n == total);
            CHECK(sstv_encoder_next_pcm(&e, pcm + n, 333) == 0);
            CHECK(sstv_encoder_line(&e) == sstv_mode_lines(mode));
            uint32_t h333 = fnv(pcm, n);
            /* the same stream in 512-sample chunks */
            sstv_encoder_init(&e, &card[0][0], 320, 240, 320, mode, rates[r].num, rates[r].den);
            uint8_t *pcm2 = malloc(total + 1024);
            uint32_t n2 = 0;
            while ((got = (uint32_t)sstv_encoder_next_pcm(&e, pcm2 + n2, 512)) > 0) n2 += got;
            CHECK(n2 == n && fnv(pcm2, n2) == h333);
            /* lead-in: 500 ms of 1900 Hz = 1900 rising zero crossings, give or take the edges */
            uint32_t lead = (uint32_t)(0.5 * rates[r].num / rates[r].den), rises = 0;
            for (uint32_t i = 1; i < lead; i++) rises += pcm[i - 1] < 128 && pcm[i] >= 128;
            CHECK(rises >= 948 && rises <= 952);
            /* the trailer is silence */
            CHECK(pcm[n - 1] == 128 && pcm[n - 100] == 128);
            printf("%-9s %-4s %9u samples (%.3f s), lead-in %u cycles\n", sstv_mode_name(mode), rates[r].name, n,
                   (double)n * rates[r].den / rates[r].num, rises);
            snprintf(path, sizeof path, "%s/%s-%s.wav", out, mnames[m], rates[r].name);
            write_wav(path, pcm, n, (rates[r].num + rates[r].den / 2) / rates[r].den);
            free(pcm); free(pcm2);
        }
    }
    CHECK(sstv_encoder_init(&e, &card[0][0], 319, 240, 320, SSTV_ROBOT36, 48000, 1) == -1);
    CHECK(sstv_encoder_init(&e, &card[0][0], 320, 240, 320, (sstv_mode_t)3, 48000, 1) == -1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
