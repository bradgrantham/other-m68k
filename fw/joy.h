/* Atari joystick / paddle port on the DUART's input and output pins (spec "joystick").
 * The switches are read straight from the input port; the paddles are timed by the DUART's
 * counter/timer and its input-change interrupt, one measurement per frame, started from the
 * VBLANK interrupt. */
#ifndef JOY_H
#define JOY_H
#include <stdint.h>
#include "syscalls.h"

int  joy_read(void);                        /* JOY_* mask, 1 = pressed */
void joy_paddles_set(int on);               /* start / stop measuring (off leaves the capacitors shorted) */
int  joy_paddles_on(void);
void joy_paddles_get(struct paddles *p);    /* the last completed frame */
void joy_vblank(void);                      /* from irq_vblank: finish the frame, start the next */
void joy_ipc(uint8_t ipcr);                 /* from irq_duart when ISR.IPC is set, with the IPCR just read */
uint32_t joy_scale(uint32_t counts);        /* counter ticks (X1/16) -> 0..PADDLE_FULL (pure; host tested) */

#endif
