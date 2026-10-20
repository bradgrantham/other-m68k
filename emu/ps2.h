/* PS/2 keyboard: SDL key events -> scan code set 2 bytes -> PS2DATA. */
#ifndef PS2_H
#define PS2_H

#include <stdint.h>

void ps2_reset(void);

/* Host key event (SDL_Scancode value), down = 1 for press/repeat. */
void ps2_key(int sdl_scancode, int down);

/* Deliver at most one queued byte to CPLD2 (one per ~1 ms, held until read). */
void ps2_tick(uint64_t now);

/* Queue the key sequence that types `text` (US layout; \n = Enter, \\ = '\'). */
void ps2_type_text(const char *text);

int ps2_queue_empty(void);

#endif
