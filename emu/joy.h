/* Model of the Atari joystick / paddle port on the DUART's IP/OP pins (spec "joystick").
 * Switches come from --joy, paddles from --paddle (fixed values, or "pad" for a game
 * controller's triggers); with a window, the first SDL game controller drives the switches
 * (d-pad or left stick, any face button = fire). */
#ifndef EMU_JOY_H
#define EMU_JOY_H
#include <stdint.h>
#include <SDL.h>

int  joy_init(const char *joy_opt, const char *paddle_opt);  /* parse the options; -1 on a bad one */
void joy_open_controller(void);                              /* after SDL_Init (not headless) */
void joy_event(const SDL_Event *ev);                         /* controller events from the main loop */
uint8_t joy_ip_bits(uint64_t now);          /* IP0..IP5 levels (bit set = high, released) */
void joy_set_charging(uint64_t now, int charging);   /* OP0 pin low (OPR bit set) = capacitors charging */
int  joy_paddles_present(void);

#endif
