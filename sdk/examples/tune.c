/* tune: plays "Ode to Joy" through the BIOS audio stream (spec rev 6).
 *
 * Two voices are synthesised as unsigned 8-bit samples at the video line rate (15707 Hz
 * NTSC; PAL is 0.5 % slower, which only shifts the pitch by that much): a square-wave
 * melody with a decaying envelope and a triangle bass one or two octaves down. Each
 * 512-sample buffer (33 ms) is encoded to PDM patterns with pdm_encode and handed to the
 * BIOS with sys_audio_next(buf, 1), which blocks until a buffer is free: the BIOS paces
 * the program to the line rate and the level-4 interrupt streams the bytes. Phases are
 * 32-bit accumulators, increments 32.0 fixed point from a one-octave table shifted down,
 * so the inner loop is adds, compares and shifts only. Any key stops it. */
#include <stdio.h>
#include <stdint.h>
#include "syscalls.h"
#include "pdm.h"

#define SAMPLE_RATE  15707u                 /* NTSC lines per second */
#define BPM          150u
#define SIXTEENTH    (SAMPLE_RATE * 60u / (BPM * 4u))   /* samples per sixteenth note */
#define BUFSIZE      512                    /* samples per hand-off, 33 ms: two buffers of slack cover a printf line and a scroll */

/* phase increment per sample for MIDI notes 108..119 (C8..B8) at SAMPLE_RATE */
static const uint32_t octave8[12] = {
    1144634364u, 1212697865u, 1284808632u, 1361207329u, 1442148929u, 1527903567u,
    1618757441u, 1715013767u, 1816993793u, 1925037866u, 2039506574u, 2160781946u,
};
static const char *const note_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static uint32_t midi_inc(int midi)          /* 0 = rest */
{
    if (midi <= 0) return 0;
    int oct_down = (119 - midi) / 12;       /* octaves below the table */
    int idx = (midi - 108) % 12;
    if (idx < 0) idx += 12;
    return octave8[idx] >> oct_down;
}

struct note { uint8_t midi, len; };          /* len in sixteenths; len 0 = print a newline */

/* Ode to Joy: lines 1, 2, bridge, 2. Bass: one note per bar (8 sixteenths each). */
static const struct note melody[] = {
    {64,4},{64,4},{65,4},{67,4}, {67,4},{65,4},{64,4},{62,4}, {60,4},{60,4},{62,4},{64,4}, {64,6},{62,2},{62,8}, {0,0},
    {64,4},{64,4},{65,4},{67,4}, {67,4},{65,4},{64,4},{62,4}, {60,4},{60,4},{62,4},{64,4}, {62,6},{60,2},{60,8}, {0,0},
    {62,4},{62,4},{64,4},{60,4}, {62,4},{64,2},{65,2},{64,4},{60,4}, {62,4},{64,2},{65,2},{64,4},{62,4}, {60,4},{62,4},{55,8}, {0,0},
    {64,4},{64,4},{65,4},{67,4}, {67,4},{65,4},{64,4},{62,4}, {60,4},{60,4},{62,4},{64,4}, {62,6},{60,2},{60,8}, {0,0},
};
static const struct note bass[] = {
    {48,16},{43,16},{48,16},{43,16},
    {48,16},{43,16},{48,16},{48,16},
    {43,16},{48,16},{43,16},{43,16},
    {48,16},{43,16},{48,16},{48,16},
};
#define N_MEL (sizeof melody / sizeof melody[0])
#define N_BAS (sizeof bass / sizeof bass[0])

struct voice {
    const struct note *seq; unsigned n, idx;
    uint32_t phase, inc;
    unsigned left;                           /* samples left in the current note */
    int amp;                                 /* current envelope amplitude */
    int done;
};

static void voice_next(struct voice *v, int announce)
{
    while (v->idx < v->n && v->seq[v->idx].len == 0) { v->idx++; if (announce) printf("\n"); }
    if (v->idx >= v->n) { v->done = 1; v->inc = 0; v->left = ~0u; v->amp = 0; return; }
    const struct note *nt = &v->seq[v->idx++];
    v->inc = midi_inc(nt->midi);
    v->left = nt->len * SIXTEENTH;
    v->amp = announce ? 40 : 32;
    if (announce && nt->midi) {
        int m = nt->midi;
        printf("%s%d ", note_names[m % 12], m / 12 - 1);
    }
}

static uint8_t samples[BUFSIZE];

int main(void)
{
    struct voice mel = { melody, N_MEL, 0, 0, 0, 0, 0, 0 };
    struct voice bas = { bass, N_BAS, 0, 0, 0, 0, 0, 0 };
    printf("tune: Ode to Joy at %u bpm, %u Hz samples. Any key stops.\n", BPM, SAMPLE_RATE);
    uint32_t t0 = sys_ticks();
    pdm_init();
    printf("pdm_init: %lu ms\n", (unsigned long)(sys_ticks() - t0) * 1000 / 60);
    uint8_t *buf = sys_audio_start(BUFSIZE);
    if (!buf) { printf("sys_audio_start failed\n"); return 1; }
    voice_next(&mel, 1);
    voice_next(&bas, 0);
    int stop = 0;
    unsigned buf_no = 0;
    while (!mel.done && !stop) {
        /* envelope: melody decays towards a floor every buffer (~33 ms), bass stays */
        if (mel.amp > 12) mel.amp -= 1;
        int a1 = mel.amp;
        unsigned i = 0;
        while (i < BUFSIZE) {
            unsigned run = BUFSIZE - i;
            if (mel.left < run) run = mel.left;
            if (bas.left < run) run = bas.left;
            uint32_t p1 = mel.phase, p2 = bas.phase, i1 = mel.inc, i2 = bas.inc;
            uint8_t *o = samples + i;
            for (unsigned k = 0; k < run; k++) {
                p1 += i1; p2 += i2;
                int v = (int32_t)p1 < 0 ? a1 : -a1;        /* square */
                int t = (int)(p2 >> 24);                     /* triangle 0..255 -> -32..31 */
                t = (t < 128 ? t : 255 - t) - 64;
                v += t >> 1;
                o[k] = (uint8_t)(128 + v);
            }
            mel.phase = p1; bas.phase = p2;
            i += run;
            mel.left -= run; bas.left -= run;
            if (mel.left == 0) voice_next(&mel, 1);
            if (bas.left == 0) voice_next(&bas, 0);
        }
        pdm_encode(buf, samples, BUFSIZE);
        /* hand the buffer to the BIOS and block until the next one is free */
        buf = sys_audio_next(buf, 1);
        if (!buf) { printf("sys_audio_next failed\n"); return 1; }
        buf_no++;
        if (sys_pollchar() >= 0) stop = 1;
    }
    /* let the queued buffers play out; the BIOS then goes to the idle pattern and switches
     * the interrupt off by itself */
    sys_audio_last(0, 1);
    printf("\n%s after %u buffers.\n", stop ? "stopped" : "done", buf_no);
    return 0;
}
