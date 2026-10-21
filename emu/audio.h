/* Audio pin (PDM shifter bit 7) rendered to a 48 kHz SDL stream and/or WAV file. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* Open the SDL audio device (if sdl_on) and/or the WAV / edge-log files.
 * Returns 0 (also when nothing is enabled) or -1 if a file cannot be created. */
int  audio_init(int sdl_on, const char *wav_path, const char *edge_log_path);
void audio_shutdown(void);
void audio_reset(void);

/* The audio pin changed to `level` at `cycle` (edges arrive in time order). */
void audio_edge(uint64_t cycle, int level);

/* Render everything up to `now` (called once per frame). */
void audio_frame(uint64_t now);

#endif
