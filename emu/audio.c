/* Audio pin renderer.
 *
 * io.c reports every transition of the audio pin (the PDM shifter's bit 7,
 * up to 8 per video line) with its cycle stamp.  Once per frame the waveform
 * is box-filtered into 48 kHz mono samples (CPU_CLOCK_HZ / 48000 = 250 clocks
 * per sample, exact; the integration is exact for any number of edges per
 * sample, so the ~126 kHz PDM stream is averaged like the board's RC low-pass
 * does), passed through a one-pole high-pass standing in for the DC-blocking
 * capacitor, and queued to SDL and/or written to a WAV file (--audio-wav).
 * If the host falls behind, the SDL queue is trimmed so latency stays around a
 * few frames; the WAV file always receives every sample.  --audio-edge-log
 * writes each edge as "cycle level" for timing checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <SDL.h>

#include "hw.h"
#include "emu.h"
#include "audio.h"

#define SAMPLE_RATE 48000
#define CLOCKS_PER_SAMPLE (CPU_CLOCK_HZ / SAMPLE_RATE)
#define AMPLITUDE 9000.0
#define MAX_QUEUED_BYTES (SAMPLE_RATE * 2 / 10)      /* 100 ms */

struct edge { uint64_t t; int level; };

static int enabled;
static SDL_AudioDeviceID dev;
static struct edge *edges;
static size_t edge_count, edge_cap;
static int level;                   /* level after the last recorded edge */
static int level_at_start;          /* level at render_pos */
static uint64_t render_pos;         /* next sample starts here */
static double hp_prev_in, hp_prev_out;
static int16_t *buf;
static size_t buf_cap;
static FILE *wav;
static uint64_t wav_samples;
static FILE *edge_log;

static void put32(FILE *f, uint32_t v)
{
    unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void put16(FILE *f, uint16_t v)
{
    unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
    fwrite(b, 1, 2, f);
}

static void wav_header(FILE *f, uint32_t nsamples)
{
    uint32_t data_bytes = nsamples * 2;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16);
    put16(f, 1);                        /* PCM */
    put16(f, 1);                        /* mono */
    put32(f, SAMPLE_RATE);
    put32(f, SAMPLE_RATE * 2);          /* byte rate */
    put16(f, 2);                        /* block align */
    put16(f, 16);                       /* bits per sample */
    fwrite("data", 1, 4, f); put32(f, data_bytes);
}

int audio_init(int sdl_on, const char *wav_path, const char *edge_log_path)
{
    enabled = 0;
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (!wav) {
            fprintf(stderr, "m68k-emu: --audio-wav %s: %s\n", wav_path, strerror(errno));
            return -1;
        }
        wav_header(wav, 0);
        enabled = 1;
    }
    if (edge_log_path) {
        edge_log = fopen(edge_log_path, "w");
        if (!edge_log) {
            fprintf(stderr, "m68k-emu: --audio-edge-log %s: %s\n", edge_log_path, strerror(errno));
            return -1;
        }
        enabled = 1;
    }
    if (!sdl_on)
        return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "m68k-emu: SDL audio init failed: %s (audio disabled)\n", SDL_GetError());
        return 0;
    }
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        fprintf(stderr, "m68k-emu: SDL_OpenAudioDevice: %s (audio disabled)\n", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(dev, 0);
    enabled = 1;
    return 0;
}

void audio_shutdown(void)
{
    if (dev) {
        SDL_CloseAudioDevice(dev);
        dev = 0;
    }
    if (wav) {
        wav_header(wav, (uint32_t)wav_samples);
        fclose(wav);
        wav = NULL;
    }
    if (edge_log) {
        fclose(edge_log);
        edge_log = NULL;
    }
    enabled = 0;
}

void audio_reset(void)
{
    edge_count = 0;
    level = level_at_start = 0;
}

void audio_edge(uint64_t cycle, int lvl)
{
    if (!enabled)
        return;
    if (edge_count == edge_cap) {
        edge_cap = edge_cap ? edge_cap * 2 : 4096;
        edges = realloc(edges, edge_cap * sizeof *edges);
        if (!edges) {
            perror("realloc");
            exit(1);
        }
    }
    edges[edge_count].t = cycle;
    edges[edge_count].level = lvl;
    edge_count++;
    level = lvl;
    if (edge_log)
        fprintf(edge_log, "%llu %d\n", (unsigned long long)cycle, lvl);
}

void audio_frame(uint64_t now)
{
    if (!enabled) {
        edge_count = 0;
        render_pos = now;
        return;
    }
    /* render_pos starts at 0, so the first frame is rendered too (the WAV then
     * holds exactly one sample per 250 clocks from power-up) */
    size_t nsamples = (size_t)((now - render_pos) / CLOCKS_PER_SAMPLE);
    if (nsamples > buf_cap) {
        buf_cap = nsamples;
        buf = realloc(buf, buf_cap * sizeof *buf);
        if (!buf) {
            perror("realloc");
            exit(1);
        }
    }

    size_t e = 0;
    int cur = level_at_start;
    for (size_t s = 0; s < nsamples; s++) {
        uint64_t t0 = render_pos + s * CLOCKS_PER_SAMPLE;
        uint64_t t1 = t0 + CLOCKS_PER_SAMPLE;
        /* Integrate the level over [t0, t1). */
        uint64_t high = 0, pos = t0;
        while (e < edge_count && edges[e].t < t1) {
            uint64_t t = edges[e].t < t0 ? t0 : edges[e].t;
            if (cur) high += t - pos;
            pos = t;
            cur = edges[e].level;
            e++;
        }
        if (cur) high += t1 - pos;
        double x = ((double)high / (double)CLOCKS_PER_SAMPLE) * 2.0 - 1.0;
        double y = x - hp_prev_in + 0.995 * hp_prev_out;
        hp_prev_in = x;
        hp_prev_out = y;
        double v = y * AMPLITUDE;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        buf[s] = (int16_t)v;
    }
    render_pos += nsamples * CLOCKS_PER_SAMPLE;
    level_at_start = cur;
    /* Drop consumed edges; keep those at or after render_pos. */
    size_t keep = 0;
    while (keep < edge_count && edges[keep].t < render_pos)
        keep++;
    if (keep) {
        memmove(edges, edges + keep, (edge_count - keep) * sizeof *edges);
        edge_count -= keep;
    }

    if (wav && nsamples) {
        for (size_t s = 0; s < nsamples; s++)
            put16(wav, (uint16_t)buf[s]);
        wav_samples += nsamples;
    }
    if (!dev)
        return;
    if (SDL_GetQueuedAudioSize(dev) > MAX_QUEUED_BYTES)
        return;                     /* host is behind: skip this frame's audio */
    if (nsamples)
        SDL_QueueAudio(dev, buf, (Uint32)(nsamples * sizeof *buf));
}
