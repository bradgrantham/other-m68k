# Meet the Griffin Jr.

The Griffin Jr. is a small computer built around the Motorola 68000, the processor that
ran the first Macintosh, the Amiga and the Atari ST. It is a much simpler machine than any
of those. The whole computer is about a dozen chips on one board, and each chip has a job
you can name.

That simplicity is the point. This is a computer you can learn all the way down: you can
type a command and watch a light come on, or change one number in memory and see one pixel
change colour on the screen. This guide starts with plugging things in, and by the last
chapter it has given you the address of every part of the machine.

## The machine at a glance

| | |
|------|----------------|
| Processor | Motorola 68000 running at 12 MHz |
| Memory | 1 megabyte of RAM. Your programs get 842 KB of it |
| Built-in storage | Drive C:, 768 KB, kept in the ROM chips. It keeps its files with the power off |
| Card storage | Drive D:, a microSD card |
| Picture | 320 pixels across and 240 down. Each pixel can be any of 256 colours |
| Text | 53 characters across and 24 lines down |
| Video output | Composite video and S-video, for an NTSC television or monitor |
| Sound | One channel, 8-bit samples, 15,700 samples a second |
| Keyboard | PS/2 |
| Controllers | One Atari-style port for a joystick or a pair of paddles |
| Serial | Two ports, A and B |
| Power | 5 volts through a USB-C socket |

## What you need

- **The Griffin Jr.** itself, as a bare board or in its case.
- **A USB-C power supply and cable.** The board needs 5 volts and up to 1 amp, which a
  phone charger or a computer's USB port can supply.
- **A PS/2 keyboard.** This is the kind with a round six-pin plug, often coloured purple.
- **A television or monitor** with a composite video input (the yellow socket) or an
  S-video input. It must accept NTSC, the colour system used in North America.
- **A video cable** to match: a cable with an RCA plug at each end, or an S-video cable.

You can start without the things below, but you will want them before long:

- **Speakers or headphones** with a 3.5 mm plug, or a cable from the audio socket to the
  television's audio input.
- **A microSD card** for your own files. It must be formatted as FAT32, which is how cards
  of 32 GB and smaller are sold. A larger card can be reformatted on another computer.
- **An Atari-style joystick or paddles** with a nine-pin plug.
- **A USB serial cable** of the "FTDI" kind with a six-pin socket, to connect the Griffin
  Jr. to another computer.

## A tour of the board

Set the board down with the two push buttons nearest you. The edge by your hands is then
the front, and the edge with the power socket is the back.

![The Griffin Jr. board seen from above.](figures/board.svg){width=100%}

### The back edge

| | | |
|--|------|------------------------|
| 1 | Power | USB-C socket. It carries power only, not data |
| 2 | Serial A | Six pins for a USB serial cable. The start-up report is sent here |
| 3 | Serial B | A second serial port. It can also power a small device such as a mouse adapter |
| 4 | Video | Composite video. Use this for most televisions |
| 5 | S-video | The same picture, sharper, for a set with an S-video input |

### The front edge

| | | |
|--|------|------------------------|
| 6 | Keyboard | PS/2 socket |
| 7 | Audio | 3.5 mm socket. Both ears get the same sound |
| 8 | SD card | microSD socket. The card is drive D: |
| 9 | Joystick | Nine-pin Atari-style port |
| 10 | Lights | A red power light, then two green lights named LED0 and LED1 |
| 11 | RESET | Starts the computer again from the beginning |
| 12 | NMI | Stops a program that will not stop and returns you to the prompt |

### The left edge

| | | |
|--|------|------------------------|
| 13 | Logic analyser | Brings out the main signals for test equipment |
| 14 | JTAG | For loading the two CPLD chips. Used when the board is built |

These two are for building and testing the board, and you will not need either of them to
use the computer.

### The chips

**The 68000 processor** is the long chip across the middle, and it does all the computing.
It reads instructions from memory one after another and carries them out, about a million
of them each second.

**The two RAM chips** hold 1 megabyte between them. RAM is the working memory: programs
run from it, and the picture on the screen is stored in it. It forgets everything when the
power goes off.

**The two ROM chips** hold the built-in software, called the BIOS, and also drive C:. They
are flash memory, which keeps its contents without power but can still be rewritten by the
computer when you ask.

**CPLD 1 and CPLD 2** are the two square chips. A CPLD is a chip full of simple logic that
is arranged by loading a design into it, and between them these two do the work of several
dozen smaller chips. CPLD 1 decides which chip answers when the processor asks for an
address, and it keeps the picture in step with the television. CPLD 2 looks after the
keyboard, the SD card, the lights, the sound and the pixels of the picture.

**The 68681** is the tall chip on the left. It runs the two serial ports, and its spare
pins are put to work reading the joystick.

**The video encoder** (15) takes the red, green and blue signals from CPLD 2 and turns
them into a signal a television understands.

**The two bus buffers** (16) pass data between the processor and the RAM. The picture and
the processor both need the RAM several times in every millionth of a second, and the buffers
let them take turns without getting in each other's way.

**The 24 MHz oscillator** is the clock. Every chip on the board takes its timing from it.

::: curious
**Why are there two RAM chips and two ROM chips?**

The 68000 moves data 16 bits at a time, but each of these memory chips stores only 8 bits
at each address. So the board uses them in pairs, with one chip holding the upper 8 bits
of every 16-bit word and its partner holding the lower 8 bits.

The 68000 counts its addresses in 8-bit bytes, which gives the upper half of each word an
even address and the lower half the odd address after it. That is why the two ROM sockets
are marked EVEN and ODD on the board.
:::

## Connecting it up

The Griffin Jr. has no power switch: it starts the moment you plug in the power cable. So
connect everything else first and leave the power until last.

1. Plug the keyboard into the keyboard socket.
2. Connect the video socket to the television's yellow video input. Use the S-video
   socket instead if your set has an S-video input.
3. Connect the audio socket to speakers, headphones or the television's audio input.
4. If you have a microSD card, push it into the card socket until it clicks.
5. Switch the television on and choose the input you used.
6. Plug in the power cable.

To switch off, unplug the power cable. There is nothing to shut down first, as long as no
program is in the middle of writing a file.

::: careful
**Put the SD card in before you connect the power, and leave it there.** The Griffin Jr.
cannot tell when a card is removed, so if you pull the card out while the power is on,
files that were being written can be damaged.

Plug the keyboard in before the power too, because PS/2 sockets were not designed for
plugging and unplugging with the power on.
:::

::: careful
**Static electricity can damage the chips.** If you have a bare board, touch something
metal that is earthed before you pick the board up, and then hold it by its edges.
:::
