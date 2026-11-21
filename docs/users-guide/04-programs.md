# The programs

The Griffin Jr. comes with a small set of programs. Some are there to be used, some show
off one part of the machine, and a few are short examples left in place for people who
want to write programs of their own. This chapter takes them one at a time.

Most of them are on drive C:, and because the monitor looks there wherever you are, you
can run any of those by typing its name. A few more come as files to copy to the SD card,
and the last section of this chapter describes that package.

| Program | What it does |
|----------|--------------------|
| `basic` | The BASIC language. Chapter 5 is all about it |
| `hello` | Prints a few facts about the machine |
| `plasma` | A moving pattern of colour |
| `bmpview` | Shows a picture file |
| `tune` | Plays a piece of music |
| `sdplay` | Plays a sound file from the SD card |
| `joytest` | Shows what the joystick and paddles are doing |
| `term` | Turns the Griffin Jr. into a terminal on serial port A |
| `ls` `cat` `abench` | Programmers' examples and a speed test |
| `sstv` | Sends a picture as sound, to a phone or a radio. On the card |
| `fstest` | Checks that files can be written and read back. On the card |

Several of the examples below use files from the guide's own SD card: a picture named
`SUNSET.BMP` and a sound named `CHIME.PCM`. Your card will not have these, so put a
picture or a sound of your own in their place. Each section says how to make one.

## hello

`hello` is the smallest useful program on the drive. You met it in chapter 3. It prints
the size of the picture, where the picture is kept in memory and how much room programs
have, and then it waits for you to press a key. It is a quick way to check that programs
load and run.

## plasma

```
plasma
```

`plasma` fills the screen with slowly swirling bands of colour and keeps them moving until
you press a key.

![`plasma`, caught in mid-swirl.](shots/plasma.png){width=62%}

Nothing here is stored as a picture. The program works out the colour of every block
afresh for each frame, about twelve times a second, which is a fair show of what the 68000
can do while the screen is being drawn.

::: curious
**Two pages, and why the picture never tears**

The Griffin Jr. keeps two complete pictures in memory, called page 0 and page 1, and a
switch chooses which one goes to the television. `plasma` always draws on the page you
cannot see. When that page is finished, it flips the switch and starts drawing on the
other one.

Because you only ever see finished pictures, the movement is clean. Chapter 6 shows how to
flip pages yourself.
:::

## bmpview

```
bmpview D:SUNSET.BMP
```

`bmpview` shows a picture file on the screen and waits for a key.

![`bmpview` showing the picture from the guide's card.](shots/bmpview.png){width=62%}

The picture must be a BMP file, which is a simple kind of picture file that almost any
paint or photo program can save. It can be any size. `bmpview` shrinks or enlarges it to
fit the screen and keeps its shape, so a picture that is not the same shape as the screen
gets black bars at its sides or at top and bottom. If you would rather fill the whole
screen and accept some stretching, add `-s`:

```
bmpview D:SUNSET.BMP -s
```

A photograph can hold millions of colours and the Griffin Jr. has 256, so `bmpview` cannot
give each dot its true colour. Instead it mixes the colours it has, putting dots of two
nearby colours side by side so that from a little distance they blend into the one it
wanted. This is called dithering, and you can see it if you look closely at the sky in a
photograph.

To make a picture of your own, open it on another computer, save or export it as BMP, and
copy it to the SD card. A picture already cut down to 320 by 240 loads fastest, because
the Griffin Jr. then has less to read.

## tune

```
tune
```

`tune` plays Beethoven's "Ode to Joy" through the audio socket, printing the name of each
note as it goes. Press any key to stop it.

```{.screen session=tune}
```

There are two voices, a melody and a bass line under it. No recording is involved: the
program works out the shape of the sound wave as it plays, 15,700 values a second, and
hands them to the BIOS a small batch at a time. Chapter 7 explains how those numbers
become sound.

## sdplay

```
sdplay -pcm D:CHIME.PCM
```

`sdplay` plays a recorded sound from a file. Where `tune` makes its sound up as it goes,
`sdplay` reads it from the card, so it can play anything you can record: speech, music or
sound effects.

```{.screen session=sdplay}
```

When the sound ends, `sdplay` reports how long it took against how long it should have
taken. If the two are close, the card kept up and nothing was lost. The last figure is the
share of the time the program spent waiting for the sound to catch up, which is time a
game could have spent on something else.

The file must be raw sound with no header, one channel, 8 bits a sample, at 15,707 samples
a second. That odd rate is the number of lines the television draws each second, because
the Griffin Jr. sends out one sample for every line. One second of sound takes about 15 KB
of card space, so a three-minute song is under 3 MB.

Most sound programs can save in this form. With the free program `ffmpeg` on another
computer, this line turns any sound file into one that `sdplay` can use:

```
ffmpeg -i song.mp3 -ac 1 -ar 15707 -f u8 SONG.PCM
```

`sdplay` has two other ways of playing:

| Command | What it plays |
|----------|--------------------|
| `sdplay -pcm FILE` | 8-bit sound at 15,707 samples a second |
| `sdplay -half FILE` | 8-bit sound at half that rate. The file is half the size and sounds duller |
| `sdplay FILE` | A file already worked into the form the sound circuit uses, which leaves more of the processor free. Chapter 7 describes it |

## joytest

`joytest` shows what the controller port is reading, a few times a second, until you press
a key. Use it to check a joystick or a set of paddles, or to find out which way a
controller thinks is up.

For a joystick, add `-j`:

```{.screen session=joystick}
```

The five places stand for up, down, left, right and the fire button. A letter appears
while that switch is closed and a dash while it is open. Here the stick is held to the
left with the button pressed.

For paddles, run it without the `-j`:

```{.screen session=paddles}
```

`A` and `B` are the two paddles. Each reads from 0 at one end of its travel to 1023 at the
other. A question mark after a number means that the Griffin Jr. got no reading from that
paddle, which is what you see on the first line before the first measurement, and what you
see all the time if no paddle is plugged in. The number at the end of the line counts
pictures drawn, so you can see how often readings arrive.

The buttons on a pair of paddles show up as the left and right switches.

::: curious
**Why the -j?**

A joystick is five switches, and reading a switch is easy. A paddle is a knob that changes
an electrical resistance, and the Griffin Jr. has no direct way to measure that. So it
times something instead: it lets a small part called a capacitor charge up through the
paddle and measures how long that takes. The further you turn the knob, the longer the wait.

The two paddles report back along the same wires as the joystick's up and down switches.
While the Griffin Jr. is timing paddles, a joystick pushed up or down would confuse it,
and so a program has to choose one or the other. `-j` chooses the joystick. Chapter 8 goes
through the port pin by pin.
:::

## sstv

```
sstv D:SUNSET.BMP
```

`sstv` is one of the programs in the card package, so it runs when a card that holds it
is in the socket.

It sends a picture as sound. Slow-scan television, or SSTV, is a way of turning a
picture into a long warbling tone that can travel over anything that carries a voice.
Radio amateurs use it to send pictures around the world, and a close relative of it
carried the first television pictures back from the Moon.

To try it, put a free SSTV app such as Robot36 on a phone, hold the phone near a speaker
connected to the Griffin Jr., and run the command above. Over the next half minute the
picture builds up on the phone, one line at a time.

![The picture `sstv` shows while it is sending.](shots/sstv.png){width=62%}

Without a file name, `sstv` sends whatever is on the screen at that moment, text and all.
Press [ESC]{.key} to stop a picture part of the way through.

A few extra words after the command change what it does:

| Option | What it does |
|----------|--------------------|
| `--mode r36` | Robot 36, which takes 36 seconds. This is what you get if you do not say |
| `--mode r72` | Robot 72. Twice as long, with better colour |
| `--mode m1` | Martin M1. Almost two minutes, and a favourite of radio amateurs |
| `--loop` | Sends the picture again and again until you press [ESC]{.key} |
| `--page 1` | Sends page 1 of the screen and not page 0 |
| `--wav D:OUT.WAV` | Writes the sound to a file on the card and plays nothing |

So `sstv --mode r72 --loop D:SUNSET.BMP` sends the picture in Robot 72, over and over.

A picture that arrives slanted means that the app has guessed the speed slightly wrong,
and most apps have a setting to straighten it. A picture with smeared patches means that
the Griffin Jr. was interrupted while sending, which should not happen unless something
else is using the machine.

## term

```
term
```

`term` turns the Griffin Jr. into a terminal, which is a screen and keyboard for another
computer. Whatever you type goes out of serial port A, and whatever comes in on serial
port A appears on the screen. Press [ESC]{.key} to leave.

```{.screen session=term}
```

Here the line of text was typed on a computer at the other end of the cable. What was
typed on the Griffin Jr. went the other way and is not shown, because a terminal leaves it
to the other computer to send back what it wants you to see. If you want to see your own
typing as well, start it with `term -echo`.

You need a serial cable to use `term`, and the other computer must be set to 38400 baud.
Chapter 8 covers the cable and the settings.

## The programmers' examples

Three programs on the drive are there mainly to be read. Their source code is part of the
software for the Griffin Jr., and chapter 10 points you to it.

**`ls`** lists a directory, much as the monitor's `dir` does. The monitor has a command of
its own called `ls`, which is another name for `dir`, and the monitor's own commands
always win. To run the program you must give its full name:

```{.screen session=lsprog}
```

The first line shows the arguments just as the program received them, which is the point
of the example.

**`cat`** prints files, like the monitor's `cat`, and can take several names at once. As
with `ls`, type `cat.elf` to get the program and not the monitor's command.

**`abench`** measures how much of the processor is left for a program while sound is
playing, for each of the ways a program can make sound. The numbers matter if you are
writing a game, and chapter 7 explains what they mean.

```{.screen session=abench}
```

## Adding and removing programs

A program is a file like any other, so you manage programs with the file commands from
chapter 3.

A program does not have to be on drive C: to run. If it is on the SD card, the monitor
finds it there, as long as no program on C: has the same name. Drive C: is small, so the
card is the best place for a large collection.

To put a program on drive C: anyway, so that it is there without the card, copy it across:

```
copy D:GAME.ELF C:
```

To make room, delete one you do not use:

```
del C:ABENCH.ELF
```

Nothing is lost for good by doing this. The programs that came on drive C: are part of the
software for the Griffin Jr., so you can copy any of them back from a card later.

## The card package

The software for the Griffin Jr. includes a folder of extra files meant for the SD card,
which is also supplied as a single file named `griffin-jr-card.zip`. Copy its contents to
your card on another computer.

| File | What it is |
|----------|--------------------|
| `SSTV.ELF` | The `sstv` program described above |
| `FSTEST.ELF` | A test that writes a file, reads it back and reports whether the two match. Useful when a card or drive C: is in doubt |
| `GUESS.BAS` and five more | The BASIC programs from chapter 5, ready to run, so that you need not type them in |
| `GREET.CMD` | The script from chapter 3 |
| `README.TXT` | A list of what is on the card |

These are kept off drive C: to leave room there for your own things. If you use one of
them often, copy it to drive C: and it will be there without the card.
