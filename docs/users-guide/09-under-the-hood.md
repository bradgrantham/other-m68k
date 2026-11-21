# Under the hood

This chapter is the map of the machine. It says what lives at every address, how the
parts get the processor's attention, and how a program asks the BIOS to do something. You
do not need any of it to use the Griffin Jr., but if you have ever wondered what a
computer is really doing, this one is small enough to see whole.

## The memory map

The 68000 can address 16 megabytes, and it does not know or care what is at any address.
It puts an address on its pins and reads or writes whatever answers. CPLD 1 looks at each
address and wakes the right chip. Most of the 16 megabytes is empty.

![The memory map. Addresses are in hex.](figures/memmap.svg){width=100%}

The map has three parts.

**RAM** fills the first megabyte, from `000000` to `0fffff`. The bottom of it belongs to
the BIOS. Your programs load at `008000` and have everything from there up to the
pictures. The two pages of the picture sit at the top.

**ROM** fills one megabyte from `e00000`. The first quarter is the BIOS and the rest is
drive C:.

**The devices** come after the ROM. Each has a handful of addresses that are not memory at
all. Writing to one of them makes something happen, and reading one reports on something
in the outside world. An address used this way is called a **register**.

If the processor asks for an address in one of the empty parts, nothing answers, and CPLD
1 tells the processor so. The result is the bus error you produced in chapter 3.

::: curious
**The trick at address zero**

When a 68000 is switched on, the first thing it does is read two numbers from addresses 0
and 4. They tell it where to keep its stack and where its first instruction is. But
address 0 is RAM, and at switch-on RAM holds nothing sensible.

So for the first moments after reset, CPLD 1 makes the ROM answer at address 0 in place of
the RAM. The processor reads its two numbers from the ROM and jumps to the BIOS. The
moment it touches an address up in the ROM's own region, CPLD 1 puts the RAM back, and the
arrangement in the picture above holds from then on.

You can see the two numbers, because the BIOS copies them into RAM once the memory test
has passed:

```{.screen session=vectors}
```

The first four bytes are `00008000`, the top of the BIOS's stack. The next four are the
address in the ROM where the BIOS begins.
:::

## The registers

These are the registers you are most likely to meet. All but those of the 68681 are 16
bits wide.

| Address | Name | What it does |
|-------|-------|--------------------|
| `f00000` | INTSTAT | Read it to see what is asking for attention, and the state of several pins |
| `f00002` | INTEN | Chooses which events may interrupt the processor |
| `f00004` | SDIO | The six wires of the SD card |
| `f00006` | GPIO | The two green lights and the keyboard's two wires |
| `f00008` | PS2DATA | The last byte the keyboard sent |
| `f0000a` | AUDIO | The sound pattern for the current line |
| `f0000e` | ID | Always reads `6802` on this board |
| `f10000` | PAGE0 | Write anything here to show page 0 |
| `f10002` | PAGE1 | Write anything here to show page 1 |
| `f20001` and up | | The 68681's registers: the serial ports and the joystick |

The monitor's `mw` command writes a 16-bit word, which makes it the right tool for
trying these out.

```{.screen session=regs9}
```

The `md` read the ID register and got `68 02`. The three `mw` commands wrote to GPIO:
the first lit LED0, the second lit both lights and the third put them out. This is all
that the `led` command does.

In the GPIO register each bit is one wire. Bit 3 is LED0 and has the value 8, and bit 4 is
LED1 and has the value hex 10. Bits 0 and 1, worth 1 and 2, are the two wires to the
keyboard, and they must be left at 1. That is where the numbers came from: hex `b` is 8 +
2 + 1, hex `1b` adds 10 for the second light, and `3` leaves only the keyboard wires.

::: careful
Always keep the bottom two bits set when you write to GPIO. If you write a number without
them, such as `mw f00006 8`, the Griffin Jr. holds the keyboard's wires low and the
keyboard stops working until you press RESET.
:::

::: try
Type `mw f10002 0`. The screen switches to page 1. Typing blind, enter `mw f10000 0` to
come back. This is what the `page` command does, and what BASIC's `PAGE` statement does.
:::

Three of these registers, INTEN, GPIO and AUDIO, can be written but not read back. There
was not enough room in CPLD 2 for the extra logic. The BIOS keeps its own note of what it
last wrote to each of them, and the `info` command prints its notes for INTEN and GPIO.

The programmer's guide lists every register and every bit.

## Interrupts

Much of what a computer does is wait. The keyboard may have a key for it, or the
television may have finished a picture, and it would be wasteful for the processor to keep
asking. So the parts of the machine tap it on the shoulder. This tap is called an
**interrupt**: the processor stops what it is doing, runs a short piece of the BIOS that
deals with the event, and goes back to where it was.

The 68000 has seven levels of interrupt. A higher level can break in on a lower one. The
Griffin Jr. uses five of them.

| Level | Source | What the BIOS does |
|----|----------|--------------------|
| 1 | The television has finished a picture | Counts a tick and keeps the clock |
| 2 | The keyboard has sent a byte | Works out which key it was and queues it |
| 3 | The 68681 | Takes in bytes from the serial port, and times the paddles |
| 4 | The television has started a new line | Sends out the next sound pattern |
| 7 | The NMI button | Prints the registers and returns to the monitor |

The order makes sense when you think about how long each can wait. Sound comes above the
rest because a late sample can be heard, whereas a key that is read a thousandth of a
second late makes no difference to anyone. The NMI button is at level 7, the one level
that can never be switched off, so that it works even when a program has gone badly
wrong.

## How a program talks to the BIOS

A program could read the keyboard and draw its own letters by working the registers
directly, but nearly all programs let the BIOS do it. The BIOS offers about forty services,
called **system calls**, for things like printing text, reading a key, opening a file,
waiting for the next picture and playing a buffer of sound.

To make a system call, a program puts the number of the service in a register and carries
out the instruction `TRAP #15`. The processor jumps into the BIOS, the BIOS does the work,
and the program carries on from the next instruction with the answer waiting for it.

This arrangement has a second purpose. The 68000 has two modes. The BIOS runs in
supervisor mode, where everything is allowed, and your programs run in user mode, where
the instructions that could freeze the whole machine are forbidden. `TRAP` is the doorway
between the two. A program that goes wrong in user mode usually ends in one of the reports
from chapter 3, with the monitor still alive to print it.

The programmer's guide describes every system call.

## Where the time goes

It can help to know how busy the 68000 is. The clock ticks 12 million times a second, and
each instruction takes from 4 to about 150 of those ticks, with most taking between 8 and
20. That comes to roughly a million instructions a second.

Some of that is always spoken for:

| Job | Share of the processor |
|----------|--------------------|
| Counting ticks and keeping the clock | Next to nothing |
| The keyboard | Next to nothing, and only when keys are pressed |
| Sound, while it is playing | About a third |
| Timing two paddles | About 2 per cent |
| Drawing the picture | Nothing. The picture never stops the processor |

Everything else belongs to your program.
