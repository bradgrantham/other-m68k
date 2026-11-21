# Switching on

Plug in the power cable and watch. Every time it starts, the Griffin Jr. spends about
five seconds testing itself before it hands the machine over to you, and most of that test
happens where you can see it.

## What happens

The red power light comes on at once and stays on for as long as the board has power.

A moment later the screen fills with fine coloured stripes, which change twice while the
two green lights are lit.

![The memory test, as it looks on the screen.](shots/ramtest.png){width=62%}

Those stripes are the memory test. The Griffin Jr. writes a number into every part of its
RAM and then reads each one back to see that it kept. The picture on the screen is stored
in that same RAM, so for a few seconds the test numbers show up as colours and you can
watch them go by.

After about four seconds the screen clears to dark blue and the opening message appears.

![The opening screen.](shots/boot.png){width=62%}

The white block after `C:/>` is the cursor, and it shows where the next thing you type
will appear. When you see it, the Griffin Jr. has finished starting up and is waiting for
you.

## Reading the opening screen

```{.screen session=boot}
```

**The first line** is the computer's name.

**The second line** is the memory your programs can use. The board has 1 megabyte of RAM
and 842 KB of it is free, because the rest holds the picture on the screen and the working
notes of the BIOS.

BIOS stands for Basic Input/Output System. It is the program in the ROM chips that starts
the machine, reads the keyboard, draws the text and runs your programs. You will meet it
by name all through this guide.

**The third line** did not come from the BIOS at all. It was printed by a file of commands
named `AUTOEXEC.CMD` on drive C:, which the Griffin Jr. carries out every time it starts.
Chapter 3 shows how to put your own commands there.

**The last line** is the prompt, which means "your turn". It also tells you where you are:
on drive `C:`, in the top directory `/`.

The opening screen is kept short on purpose. If you want the version of the BIOS and the
other facts that identify your machine, the `info` command in chapter 3 prints them.

## The lights

The two green lights show how far the start-up has gone. They go by quickly when all is
well, but if the machine ever stops part of the way, the lights tell you where.

| LED0 | LED1 | Meaning |
|-----|-----|--------------------|
| on | off | The Griffin Jr. has started and is checking the ROM |
| on | on | Testing the RAM. This is the long step |
| off | on | The RAM passed, and the BIOS is setting itself up |
| off | off | Ready |
| taking turns, twice a second | | The RAM failed the test |

Once the prompt appears the BIOS has no more use for the lights, so they are yours.
Chapter 3 shows how to switch them on and off.

## The two buttons

**RESET** starts the Griffin Jr. again, just as if you had unplugged it and plugged it
back in. The memory test runs again and wipes whatever was in RAM, but the files on
drives C: and D: are safe.

**NMI** stops whatever is running and brings back the prompt, which makes it the button
to reach for when a program has stopped answering the keyboard. Before the prompt returns,
the Griffin Jr. prints a few lines of numbers that show what the processor was doing at
the moment you pressed the button. Chapter 3 explains how to read them.

Try NMI first, because it leaves everything in RAM as it was. RESET is for when NMI does
not help.

::: careful
Do not press NMI while the memory test is running. The button works through a table that
is kept in RAM, and the test is busy overwriting that table. If you press it by mistake,
press RESET and let the machine start again.
:::

## Starting without AUTOEXEC.CMD

If you hold down [ESC]{.key} during the memory test, the Griffin Jr. skips `AUTOEXEC.CMD`
and goes straight to the prompt. This is your way back in if you ever put a command in
that file and regret it, such as a program that never ends.

## If something goes wrong

**The power light stays off.** Check both ends of the power cable, then try another power
supply or another cable. Some cables made only for charging small things such as
headphones are too thin to carry enough current.

**The lights work but there is no picture.** Check that the television is set to the input
you used, and that the cable is in the socket marked VIDEO and not in the audio socket.
Then watch the green lights as you plug in the power. If they come on and both go off
again after a few seconds, the computer has started properly and the fault is in the cable
or the television.

**The picture rolls or has no colour.** The television may not accept NTSC, so try another
set.

**The green lights take turns, on and off, without stopping.** The RAM failed its test.
Unplug the power, check that the two RAM chips are pressed fully into their sockets, and
try again.

**The opening screen says `ROM BAD`.** The BIOS in the ROM chips has been damaged. If the
prompt still appears, the machine can put a fresh copy in place by itself, and the
appendix on updating the flash describes how.

**The prompt appears but typing does nothing.** Unplug the power, check the keyboard plug,
and plug the power back in.

::: curious
**The report you can read without a screen**

While it tests itself, the Griffin Jr. also sends a short report out of serial port A:

```
Griffin Jr. POST
ROM OK
RAM ... OK
```

POST stands for power-on self test. Each dot after `RAM` is one pass of the memory test,
and if a pass fails, the report gives the address that failed, the number that was written
there and the number that came back.

The self test cannot trust the RAM, because the RAM is the thing being tested. For that
reason this part of the BIOS is written to use no memory at all, and it keeps everything
it needs in the processor's own registers.

The report means that a board with no picture can still tell you what is wrong with it.
Chapter 8 shows how to connect a serial cable and read it.
:::

::: curious
**How the ROM checks itself**

The BIOS is 256 KB, which is 131,072 words of 16 bits. When the BIOS is built, its last
word is chosen so that all the words added together come to exactly zero, ignoring any
carry past 16 bits. At start-up the Griffin Jr. adds them up again, and if even one bit
has changed since the BIOS was built, the total is no longer zero.

The `info` command prints that last word after `sum`, followed by `(ok)` if the total came
to zero. The word is different in every version of the BIOS.
:::
