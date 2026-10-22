/* joytest: show the Atari port live. The joystick mask is read every frame; the paddles
 * are measured by the BIOS once a frame after the first sys_paddles call. Any key quits.
 *   joytest          joystick and paddles
 *   joytest -j       joystick only (paddle measuring stays off, so up/down are plain switches) */
#include <stdio.h>
#include <string.h>
#include "syscalls.h"

int main(int argc, char **argv)
{
    int with_paddles = !(argc > 1 && !strcmp(argv[1], "-j"));
    struct paddles p = { 0, 0, 0, 0 };
    printf("joystick port (any key quits)\n");
    for (int frame = 0;; frame++) {
        sys_wait_vblank();
        int j = sys_joystick();
        if (with_paddles) sys_paddles(&p);
        if (frame % 10 == 0) {                          /* six lines a second */
            printf("\r%s%s%s%s%s   ", (j & JOY_UP) ? "U" : "-", (j & JOY_DOWN) ? "D" : "-", (j & JOY_LEFT) ? "L" : "-",
                   (j & JOY_RIGHT) ? "R" : "-", (j & JOY_FIRE) ? "F" : "-");
            if (with_paddles)
                printf("A %4lu%s B %4lu%s f%lu", (unsigned long)p.a, (p.valid & PADDLE_A_VALID) ? " " : "?",
                       (unsigned long)p.b, (p.valid & PADDLE_B_VALID) ? " " : "?", (unsigned long)p.frame);
            printf("\n");
        }
        if (sys_pollchar() >= 0) break;
    }
    if (with_paddles) sys_paddles(0);
    return 0;
}
