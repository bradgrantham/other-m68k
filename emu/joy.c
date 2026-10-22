/* See joy.h. Paddle timing: a paddle at position p (0..1023) ends its charge
 * p / 1023 x PADDLE_FULL_SCALE_US after the release, which is when the 74HC14 pulls the
 * up (A) or down (B) line low; both lines go high again as soon as the capacitors are
 * shorted. A joystick never pulls those lines by itself. The DUART samples the lines on
 * every duart_tick, i.e. once per video line, so the emulated paddle resolution is about
 * 64 us, 0.5 % of full scale. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw.h"
#include "syscalls.h"
#include "joy.h"

static int mask;                    /* JOY_* bits pressed */
static int paddles;                 /* 0 none, 1 fixed values, 2 controller triggers */
static int pos[2] = { 512, 512 };
static int charging;
static uint64_t release_cycle;
static SDL_GameController *pad;
static int stick_mask, dpad_mask, button_mask;

#define FULL_CYCLES ((uint64_t)PADDLE_FULL_SCALE_US * CPU_CLOCK_HZ / 1000000u)

int joy_init(const char *joy_opt, const char *paddle_opt)
{
    if (joy_opt) {
        char buf[128]; strncpy(buf, joy_opt, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        for (char *w = strtok(buf, ","); w; w = strtok(NULL, ",")) {
            if (!strcmp(w, "up")) mask |= JOY_UP;
            else if (!strcmp(w, "down")) mask |= JOY_DOWN;
            else if (!strcmp(w, "left")) mask |= JOY_LEFT;
            else if (!strcmp(w, "right")) mask |= JOY_RIGHT;
            else if (!strcmp(w, "fire")) mask |= JOY_FIRE;
            else if (!strcmp(w, "none")) ;
            else { fprintf(stderr, "m68k-emu: --joy: unknown switch '%s' (up,down,left,right,fire)\n", w); return -1; }
        }
    }
    if (paddle_opt) {
        if (!strcmp(paddle_opt, "pad")) paddles = 2;
        else {
            int a, b;
            if (sscanf(paddle_opt, "%d,%d", &a, &b) != 2 || a < 0 || b < 0 || a > PADDLE_FULL || b > PADDLE_FULL) {
                fprintf(stderr, "m68k-emu: --paddle wants A,B (0..%d) or 'pad'\n", PADDLE_FULL); return -1;
            }
            pos[0] = a; pos[1] = b; paddles = 1;
        }
    }
    return 0;
}

int joy_paddles_present(void) { return paddles != 0; }

void joy_open_controller(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) { fprintf(stderr, "m68k-emu: game controller '%s' on the joystick port\n", SDL_GameControllerName(pad)); break; }
        }
}

static void update_mask(void)
{
    mask = stick_mask | dpad_mask | button_mask;
}

void joy_event(const SDL_Event *ev)
{
    if (!pad) return;
    switch (ev->type) {
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int down = ev->type == SDL_CONTROLLERBUTTONDOWN, bit = 0;
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: bit = JOY_UP; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: bit = JOY_DOWN; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: bit = JOY_LEFT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: bit = JOY_RIGHT; break;
        case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_X: case SDL_CONTROLLER_BUTTON_Y: bit = JOY_FIRE; break;
        default: break;
        }
        if (bit & JOY_FIRE) { if (down) button_mask |= JOY_FIRE; else button_mask &= ~JOY_FIRE; }
        else if (bit) { if (down) dpad_mask |= bit; else dpad_mask &= ~bit; }
        update_mask();
        break;
    }
    case SDL_CONTROLLERAXISMOTION: {
        int v = ev->caxis.value;
        switch (ev->caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            stick_mask &= ~(JOY_LEFT | JOY_RIGHT);
            if (v < -16000) stick_mask |= JOY_LEFT; else if (v > 16000) stick_mask |= JOY_RIGHT;
            break;
        case SDL_CONTROLLER_AXIS_LEFTY:
            stick_mask &= ~(JOY_UP | JOY_DOWN);
            if (v < -16000) stick_mask |= JOY_UP; else if (v > 16000) stick_mask |= JOY_DOWN;
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            if (paddles == 2) pos[0] = v * PADDLE_FULL / 32767;
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            if (paddles == 2) pos[1] = v * PADDLE_FULL / 32767;
            break;
        default: break;
        }
        update_mask();
        break;
    }
    default: break;
    }
}

void joy_set_charging(uint64_t now, int on)
{
    if (on && !charging) release_cycle = now;
    charging = on;
}

uint8_t joy_ip_bits(uint64_t now)
{
    uint8_t ip = 0x3F;
    if (mask & JOY_UP) ip &= (uint8_t)~JOY_IP_UP;
    if (mask & JOY_DOWN) ip &= (uint8_t)~JOY_IP_DOWN;
    if (mask & JOY_LEFT) ip &= (uint8_t)~JOY_IP_LEFT;
    if (mask & JOY_RIGHT) ip &= (uint8_t)~JOY_IP_RIGHT;
    if (mask & JOY_FIRE) ip &= (uint8_t)~JOY_IP_FIRE;
    if (paddles && charging) {
        for (int i = 0; i < 2; i++) {
            uint64_t t = (uint64_t)pos[i] * FULL_CYCLES / PADDLE_FULL;
            if (now >= release_cycle + t) ip &= (uint8_t)~(i == 0 ? JOY_IP_UP : JOY_IP_DOWN);
        }
    }
    return ip;
}
