/* 1-bit audio on GPIO.AUDIO rendered to a 48 kHz SDL stream. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* Open the SDL audio device.  Returns 0 (also when disabled) or -1. */
int  audio_init(int enabled);
void audio_shutdown(void);
void audio_reset(void);

/* GPIO.AUDIO changed to `level` at `cycle`. */
void audio_edge(uint64_t cycle, int level);

/* Render everything up to `now` (called once per frame). */
void audio_frame(uint64_t now);

#endif
