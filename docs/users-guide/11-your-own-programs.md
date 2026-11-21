# Writing your own programs

BASIC is the quickest way to make the Griffin Jr. do something new, and for many purposes
it is all you will need. But the programs on drive C: were not written in BASIC. They were
written in C on another computer and carried across, and they run many times faster than
BASIC can. This chapter is a short account of how that is done, so that you
know what is possible. The programmer's guide is the full manual.

## The idea

The 68000 does not understand C or BASIC. It understands only its own instructions, which
are numbers. A **compiler** is a program that translates C into those numbers, and a
compiler that runs on one kind of computer and produces instructions for another is
called a cross compiler.

So a program for the Griffin Jr. is written and compiled on a modern computer. The result
is a file ending in `.ELF`, which you copy to the SD card and run by typing its name.

Everything needed is free: the GNU C compiler built for the 68000, and the Griffin Jr.
software kit, which is called the SDK. The SDK supplies the pieces that connect a C
program to the BIOS, the libraries for pictures and sound, and the source of every program
on drive C: to learn from.

## A first program

This is a whole program.

```
#include <stdio.h>

int main(void)
{
    printf("Hello from my own program.\n");
    return 0;
}
```

To build it, save it in the SDK's `examples` directory as `mine.c`, add the word `mine`
to the list of examples in the SDK's `Makefile`, and type `make`. Out comes `mine.elf`.
Copy that to the SD card, put the card in the Griffin Jr., and type:

```
mine
```

The standard C library is there, so `printf`, `fopen`, `malloc` and the rest behave as
they do on any computer. Files opened with `fopen` are files on drives C: and D:.

For anything particular to the Griffin Jr., a program makes system calls, which chapter 10
introduced. In C they look like ordinary functions whose names begin with `sys_`:

| Function | What it does |
|----------|--------------------|
| `sys_video_info` | Tells the program where the two pages of the picture are |
| `sys_video_set_page` | Chooses the page the television shows |
| `sys_wait_vblank` | Waits for the television to finish the current frame |
| `sys_pollchar` | Gives a key if one has been pressed, without waiting |
| `sys_joystick` | Reads the joystick's switches |
| `sys_paddles` | Reads the two paddles |
| `sys_audio_start` | Begins a stream of sound and returns the first buffer to fill |
| `sys_ticks` | The count of frames drawn since the machine started |

To draw, a program asks `sys_video_info` for the address of a page and then writes bytes
to it, exactly as you did by hand with `fill` in chapter 7. `plasma` is the example to
read first: it is short, and it shows drawing, page flipping and the keyboard.

## Trying a program without the card

Carrying the card back and forth for every change soon becomes tiresome. There are two
ways round it.

**Send the program down the serial cable.** The build also produces `mine.srec`, which is
the same program written out as text. Type `srec` at the monitor's prompt, send the file
from the other computer, and when the monitor reports that it has arrived, type `go` to
run it. At 38400 baud a small program takes about half a minute to send.

**Use the emulator.** The software for the Griffin Jr. includes an emulator, which is a
program that behaves like the whole machine: processor, picture, sound, keyboard, card and
joystick. It runs the real BIOS and the real programs in a window on a modern computer.
Most of the software in this guide was written and tested on it before any board existed,
and every screen shown in this guide was captured from it.

## Going further

A C program can do everything the BIOS can, and more. It can take over an interrupt, which
is how a game keeps perfect time. It can use serial port A for anything it likes. It can
load and start another program.

Languages other than C are possible too. The compiler also handles C++, and the 68000
has been a target for nearly every language ever written, from Forth and Pascal to Lisp.
Anything that can produce 68000 instructions and make a `TRAP #15` can be made to run
here.
