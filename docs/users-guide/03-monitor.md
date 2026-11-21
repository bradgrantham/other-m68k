# Talking to the monitor

The program that prints `C:/>` and waits for you is called the monitor. It is part of the
BIOS, so it is there from the moment the Griffin Jr. starts. You type a command and press
[Enter]{.key}, the monitor carries it out, and then it prints the prompt again to show
that it is ready for the next one.

This chapter covers the commands you will use every day. Each is short enough to try as
you read, and trying them is the quickest way to learn them.

## Your first commands

Type `help` and press [Enter]{.key}.

```{.screen session=help}
```

This is the whole list, squeezed to fit on one screen. There is no need to learn it now,
because this chapter and the ones after it go through the commands a few at a time.

Capital and small letters are the same to the monitor, so `HELP`, `help` and `Help` all
work. The same goes for the names of programs and files.

If you type something the monitor does not know, it says so and nothing else happens.

```{.screen session=unknown}
```

You cannot harm the Griffin Jr. by typing. Only three commands can erase what is stored on
a drive or in the ROM, which are `del`, `format` and `flash`, and the last two ask before
they act.

### Lights and sound

Now make the machine do something you can see and hear.

```{.screen session=firsts}
```

`beep` plays a short tone through the audio socket.

`led` switches the two green lights: `led 1` lights LED0, `led 2` lights LED1, `led 3`
lights both and `led 0` puts them out.

`ticks` prints a counter that goes up by one each time the television finishes drawing a
picture. That happens 60 times a second, which is why the second number above is larger
than the first. Type `ticks`, count ten seconds and type it again, and the two numbers
will be about 600 apart.

::: curious
**The monitor counts in hexadecimal**

The numbers you give to the monitor's commands are in hexadecimal, or hex for short, and
so are the addresses and bytes it prints. Hex is counting in sixteens. It uses the digits
0 to 9 and then the letters `a` to `f` for ten to fifteen.

| Hex | 1 | 9 | a | f | 10 | 1f | 20 | 64 | ff | 100 |
|---|---|---|---|---|---|---|---|---|---|---|
| Decimal | 1 | 9 | 10 | 15 | 16 | 31 | 32 | 100 | 255 | 256 |

Computers are built from switches that are either on or off. One hex digit stands for
exactly four of those switches and two hex digits stand for one byte of memory, which
makes hex the easiest way to write down what is inside the machine.

It makes no difference for `led`, because 0 to 3 are the same either way. It will matter
later in this chapter, where `20` means thirty-two. Counters meant for people, such as
`ticks` and the file sizes in a listing, are printed in ordinary decimal.
:::

Two more commands are useful right away. `cls` clears the screen, and `info` prints the
facts that identify your machine.

```{.screen session=info}
```

The first line gives the version of the BIOS. The second gives the size of the BIOS and
the result of the check on it, then the size of drive C: and the size of the RAM. The
third says that the picture is NTSC and that serial port A is set to 38400 baud. The lines
after that will mean more once you have read chapter 9.

## Typing and fixing mistakes

Nothing happens until you press [Enter]{.key}, so you can change a line as much as you
like before then.

| Key | What it does |
|----------|--------------------|
| [←]{.key} [→]{.key} | Move the cursor along the line |
| [Home]{.key} [End]{.key} | Jump to the start or the end of the line |
| [Backspace]{.key} | Rub out the character before the cursor |
| [Delete]{.key} | Rub out the character under the cursor |
| [Ctrl]{.key} + [U]{.key} | Rub out the whole line |
| [Ctrl]{.key} + [K]{.key} | Rub out from the cursor to the end |
| [Ctrl]{.key} + [W]{.key} | Rub out the word before the cursor |
| [↑]{.key} [↓]{.key} | Bring back lines you typed earlier |
| [Tab]{.key} | Finish the word for you |
| [ESC]{.key} | Stop a long listing or a file copy |

The monitor remembers the last 32 lines you typed. Press [↑]{.key} to step back through
them, change one if you like, and press [Enter]{.key} to use it again. If you want to see
them all at once, the `history` command lists them.

```{.screen session=history}
```

[Tab]{.key} saves typing. Type the first few letters of a command, a program or a file
name and press [Tab]{.key}. If only one name starts that way, the monitor fills in the
rest. If several do, it fills in as much as they have in common, and pressing
[Tab]{.key} a second time shows you the choices.

::: try
Type `pl` and press [Tab]{.key}. The monitor completes it to `PLASMA`, the name of a
program on drive C:. Press [Enter]{.key} to run it, and press any key when you have seen
enough.
:::

## Drives, files and directories

A **file** is a named piece of stored information, such as a program, a picture or a page
of text. A **drive** is a place that holds files, and the Griffin Jr. has two of them.

**Drive C:** is inside the ROM chips, so it is always there. It holds 768 KB, and the
programs that come with the Griffin Jr. live on it.

**Drive D:** is the microSD card. It holds far more than C:, and you can take it out and
carry it to another computer.

### Listing files

`dir` lists the files in the place where you are.

```{.screen session=dir}
```

Each line gives a file's name, its size in bytes, and the date and time it was last
changed, with the date written as year, month and day. The last line counts the files and
adds up their sizes.

The three letters after the dot tell you what kind of file it is. Files ending in `.ELF`
are programs, files ending in `.CMD` are lists of monitor commands, and `.TXT` is plain
text.

### Reading a file

`cat` prints a text file on the screen.

```{.screen session=cat}
```

A file longer than the screen scrolls by faster than you can read it, and pressing
[ESC]{.key} stops it.

### Changing drives

To go to a drive, type its name. The prompt changes to show where you are now.

```{.screen session=drives}
```

You can also name a drive without going there. From drive C:, for example, `dir D:` lists
the card and leaves you where you were.

The card in these pictures holds a few files that were made for this guide, so your own
card will show something different.

If the prompt shows a question mark, as in `D:?>`, the Griffin Jr. cannot read that drive.
For D: this means that there is no card in the socket, or that the card is not formatted
as FAT32.

### Directories

A **directory** is a named group of files inside a drive, and it is what other computers
call a folder. A card can hold thousands of files, and directories keep them tidy.

`mkdir` makes a directory, `cd` moves into one, and `rmdir` removes one once it is empty.
`cd ..` moves back out, and `cd` by itself prints where you are.

```{.screen session=cd}
```

The full name of a file gives its drive, then its directories, then its name, with slashes
between them: `D:/GAMES/PONG.ELF`. You can use a full name anywhere a file name is wanted.
If you leave out the drive or the directory, the monitor uses the ones shown in the
prompt.

Names can be up to 63 characters long. A name with a space in it must go in double quotes,
as in `cat "My notes.txt"`, so that the monitor does not take it for two names.

### Copying, renaming and deleting

| Command | What it does |
|----------|--------------------|
| `copy FROM TO` | Makes a copy of a file. `TO` can be a new name, a directory or a drive |
| `ren OLD NEW` | Gives a file a new name, or moves it to another directory on the same drive |
| `del NAME` | Deletes a file for good |

Here a file is copied from the card to drive C:, renamed, listed and then deleted.

```{.screen session=files}
```

::: careful
`del` does not ask first, and there is no way to get a deleted file back.
:::

### Many files at once

Two special characters can stand in for parts of a name. A star `*` matches any run of
characters, and a question mark `?` matches any one character.

```{.screen session=wild}
```

These work with `copy` and `del` as well. `copy D:*.ELF C:` copies every program on the
card to drive C: in one go. `del *.TXT` deletes every text file in the current directory
just as quickly, so read that one twice before you press [Enter]{.key}.

::: curious
**Where is drive C:?**

The two ROM chips hold 1 megabyte between them. The BIOS takes the first 256 KB, and the
rest is laid out like a small disk. That is drive C:.

Flash memory can only be erased a block at a time. On the Griffin Jr. a block is 8 KB, and
erasing and rewriting one takes about a tenth of a second, during which the machine can do
nothing else. That is why writing a file to C: is slower than reading one, and why sound
that is playing at that moment will stutter.

Each block can be erased about 100,000 times before it wears out. Ordinary use will never
come near that, but a program that rewrites a file over and over should keep it on the SD
card instead.
:::

## Running programs

To run a program, type its name without the `.ELF`.

```{.screen session=hello}
```

`hello` prints some facts about the machine and then waits for a key. When a program ends,
the prompt comes back.

The monitor looks for a program in three places: first the directory you are in, then
drive C:, then drive D:. This is why the programs on C: work wherever you happen to be.

Many programs need to be told what to work on. You put that after the program's name with
a space between, as in `bmpview D:PHOTO.BMP`, and the words after the name are called
**arguments**. Chapter 4 describes each program and the arguments it takes.

If a program gets stuck and will not give the prompt back, press the NMI button.

## The clock

The Griffin Jr. keeps the date and time and stamps them on every file it writes. It has
no battery, though, so the clock starts unset each time you plug in the power.

```{.screen session=date}
```

`date` by itself shows the clock and offers to change it, and pressing [Enter]{.key}
leaves it alone. `date` followed by a date and a time sets the clock in one step. The time
uses the 24-hour clock, so half past two in the afternoon is `14:30`.

If you never set the clock, the machine still works, but every file it writes is stamped
1 January 2026.

## Scripts

A **script** is a text file of monitor commands with a name ending in `.CMD`. When you
type its name, the monitor carries out each line as if you had typed it yourself.

Here is one from the guide's card, followed by what happens when it runs.

```{.screen session=script}
```

Lines that start with `#` or `;` are notes for people, and the monitor skips them. `echo`
prints the rest of its line on the screen. A script can run programs and it can run other
scripts, and you can stop one between lines by pressing [ESC]{.key}.

### AUTOEXEC.CMD

One script is special. If drive C: has a file named `AUTOEXEC.CMD`, the Griffin Jr. runs
it every time it starts. This is the one that comes with the machine.

```{.screen session=autoexec}
```

Everything but the last line is a note. The last line prints the message you saw on the
opening screen.

Here are some things you might put in your own:

- `date`, so that the Griffin Jr. asks you for the date and time whenever it starts.
- `D:`, so that you start on the SD card.
- The name of a program, so that it runs at once. A Griffin Jr. that starts straight into
  a game or a slide show is one line away.

The monitor cannot write text files by itself. For now, write your script on another
computer, save it to the SD card as plain text, and copy it across:

```
copy D:AUTOEXEC.CMD C:
```

If your new `AUTOEXEC.CMD` turns out to be a mistake, remember that holding [ESC]{.key}
during the memory test skips it.

## Looking into memory

Everything the Griffin Jr. knows is a number stored at an **address**, and the monitor
lets you look at any address and change what is there. Chapter 9 has the full map of what
lives where. This is a first look.

```{.screen session=md}
```

`md` stands for memory dump. The command `md e00000 20` shows hex 20 bytes, which is 32,
starting at address `e00000`. Each line begins with an address and then shows eight bytes
in hex. At the end of the line are the same eight bytes again as characters, with a dot
for any byte that is not a letter, a digit or a sign.

Address `e00000` is where the ROM starts, so the first command shows the opening bytes of
the BIOS.

Address `8000` is in RAM, at the place where your programs go. Nothing has run there yet,
and the pattern `55 aa` is what the memory test left behind.

`mb` stands for modify bytes. It writes the bytes you give it into memory, starting at the
address. The second `md` shows them in place, and the characters at the end of the line
spell out what was written.

### Painting with numbers

The picture on the screen is made of tiny squares called pixels, and it is stored in RAM
with one byte for each pixel. If you change a byte, a pixel changes colour. The `fill` command writes the same byte to a whole range of
addresses, which makes it a paintbrush.

::: try
Type `cls`, then these two lines:

```
fill f5100 1900 e0
fill f6a00 1900 1c
```

A red stripe and a green stripe appear across the middle of the screen.
:::

![Two `fill` commands, writing straight into the picture.](shots/paint.png){width=62%}

The first number is the address to start at, the second is how many bytes to fill and the
third is the byte to write. Hex `1900` bytes is 6,400 pixels, which is 20 rows of 320. The
byte `e0` is bright red and `1c` is bright green. Try `03` for blue and `ff` for white.
Chapter 6 explains how a byte becomes a colour, and `cls` wipes the stripes away when you
have finished.

::: careful
`md` only looks, but `mb` and `fill` change memory, and the monitor will not stop you from
writing over its own working memory below address `8000`. If the Griffin Jr. starts
behaving strangely after an experiment, press RESET. No harm is done.
:::

## When something goes wrong

Sometimes the processor is asked to do something impossible. A program might ask for an
address where there is nothing, or try to carry out a byte that is not an instruction.
The Griffin Jr. catches these and tells you what happened, and since nothing lives at
address `800000`, you can cause one yourself.

```{.screen session=fault}
```

The first line names the problem. A **bus error** means that the processor asked for an
address and nothing answered.

The lines after it are a picture of the processor at that moment.

| | |
|-----|--------------------------|
| `PC` | The program counter: the address of the instruction that was running |
| `SR` | The status register: the processor's own switches and flags |
| `fault` | The address that caused the trouble, and whether it was being read or written |
| `D0` to `D7` | The eight data registers, where the processor holds numbers it is working on |
| `A0` to `A7` | The eight address registers, which hold the addresses it is working with |
| `SSP`, `USP` | The two stack pointers, one for the BIOS and one for your program |

After the report the prompt comes back and you can carry on as before. If the report has
scrolled away, `regs` prints it again.

Pressing the NMI button prints the same kind of report with `Exception 31` on its first
line. Nothing has gone wrong in that case. The report is there so that the `PC` value can
tell you where the program was when you stopped it.

## The everyday commands

| Command | What it does |
|----------|--------------------|
| `help` | Lists the commands |
| `cls` | Clears the screen |
| `C:` `D:` | Goes to that drive |
| `dir` | Lists files. `dir D:` lists another place, `dir *.TXT` lists matching names |
| `cd NAME` | Moves into a directory. `cd ..` moves out, `cd` shows where you are |
| `mkdir NAME` `rmdir NAME` | Makes a directory, or removes an empty one |
| `cat NAME` | Prints a text file |
| `copy FROM TO` | Copies files |
| `ren OLD NEW` | Renames or moves a file |
| `del NAME` | Deletes files |
| `date` | Shows or sets the clock |
| `NAME` | Runs the program `NAME.ELF` or the script `NAME.CMD` |
| `history` | Lists the lines you have typed |
| `beep` | Plays a short tone |
| `led N` | Switches the green lights, `0` to `3` |
| `ticks` | Prints the picture counter |
| `info` | Prints the facts that identify the machine |
| `md ADDRESS LENGTH` | Shows memory |
| `mb ADDRESS BYTES` | Changes memory |
| `fill ADDRESS LENGTH BYTE` | Fills memory with one byte |
| `regs` | Prints the last fault report again |
| `reset` | The same as pressing RESET |

The commands not covered here deal with the SD card, the serial port and the ROM itself.
They come up in chapters 8 and 9, and the appendix lists every command in full.
