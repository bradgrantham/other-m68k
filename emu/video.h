/* Video timing (CPLD1 counters), framebuffer rendering and the SDL window. */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stdio.h>

struct video_timing {
    const char *name;
    int line_clocks;
    int h_active, h_front, h_sync, h_back;
    int lines;
    int v_active, v_bottom, v_sync, v_top;
    double frame_hz;
};

/* Open the window (unless headless).  Returns 0 or -1. */
int  video_init(int scale, int headless);
void video_shutdown(void);
void video_reset(void);

/* Timing of the frame in progress (latched at the start of line 0). */
const struct video_timing *video_timing(void);
int  video_page(void);                     /* page being displayed this frame */

/* Called by the main loop at the start of each scanline slice (the slice
 * begins at the HSYNC leading edge); v == 0 latches the soft switches. */
void video_line_start(int v, uint64_t cycle);

/* Live HSYNCPIN/DEPIN/VSYNCPIN bits for INTSTAT, from the current position
 * within the line. */
uint16_t video_live_pins(void);

/* Convert the displayed framebuffer page to ARGB (end of active video). */
void video_render(void);

/* Show the rendered frame and update the window title.  Returns 0, or -1 if
 * there is no window. */
int  video_present(void);

/* Pointer to the 320x240 ARGB8888 image (for tests/screenshots). */
const uint32_t *video_pixels(void);
void video_text_dump(FILE *out);      /* 40x30 console text decoded from page 0 (--text-dump) */

#endif
