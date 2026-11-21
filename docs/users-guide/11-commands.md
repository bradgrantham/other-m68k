```{=typst}
#appendices()
#set heading(numbering: appendix-numbering)
```

# Every monitor command

Numbers given to these commands are in hex. Capital and small letters are the same.
Square brackets mark something that can be left out.

## Files and drives

| Command | What it does |
|----------|--------------------|
| `C:` `D:` | Goes to that drive |
| `cd [PATH]` | Goes to a directory. With nothing after it, prints the current drive and directory. `cd D:/GAMES` sets the directory on D: without leaving the drive you are on |
| `dir [PATH]` | Lists a directory, or the files matching a pattern such as `*.BAS`. `ls` is the same command. [ESC]{.key} stops a long listing |
| `cat FILE` | Prints a text file. `type` is the same command. [ESC]{.key} stops it |
| `copy FROM TO` | Copies a file. `FROM` may be a pattern, in which case `TO` must be a directory or a drive. The copy keeps the date of the original. `cp` is the same command |
| `ren OLD NEW` | Renames a file, or moves it to another directory on the same drive. `mv` is the same command |
| `del FILE` | Deletes a file, or every file matching a pattern. Does not ask first. `rm` is the same command |
| `mkdir NAME` | Makes a directory |
| `rmdir NAME` | Removes a directory, which must be empty |
| `format C:` | Erases everything on drive C: and leaves it empty. Asks first |
| `sd` | Starts the SD card again and prints what kind it is |
| `sd rd BLOCK` | Shows one 512-byte block of the card in hex |

## Programs and scripts

| Command | What it does |
|----------|--------------------|
| `NAME [ARGUMENTS]` | Runs the program `NAME.ELF` or the script `NAME.CMD`. The monitor looks in the current directory, then on C:, then on D: |
| `run [-v] FILE [ARGUMENTS]` | Loads and runs a program file given by its full name. With `-v`, prints where in memory it was loaded |
| `go [ADDRESS]` | Runs whatever is in memory at the address. With no address, runs the last program loaded |
| `srec` | Receives a program in S-record form from serial port A. [ESC]{.key} gives up |
| `echo TEXT` | Prints the text. Useful in scripts |
| `exit` | Ends the script that is running |
| `date [YYYY-MM-DD HH:MM]` | Sets the clock. With nothing after it, shows the clock and offers to change it |
| `history` | Lists the last 32 lines you typed |

In a script, a line that begins with `#` or `;`, or with the word `rem`, is a remark.

## The machine

| Command | What it does |
|----------|--------------------|
| `help` | Lists the commands. `?` does the same |
| `info` | Prints the BIOS version, the check on the ROM, sizes, the state of several registers and the clock |
| `cls` | Clears the screen |
| `beep` | Plays a short tone |
| `led N` | Sets the green lights: 0 for neither, 1 for LED0, 2 for LED1, 3 for both |
| `page N` | Shows page 0 or page 1 of the picture |
| `ticks` | Prints the count of pictures drawn since start-up, in decimal |
| `reset` | Starts the machine again, memory test and all |
| `reboot` | Starts the BIOS again without the reset that `reset` gives the other chips |
| `ramtest` | Tests the RAM that programs and pictures use. The screen and any program in memory are lost |

## Memory

| Command | What it does |
|----------|--------------------|
| `md [ADDRESS] [LENGTH]` | Shows memory, eight bytes to a line. With no address, carries on from where the last `md` stopped |
| `mb ADDRESS BYTE ...` | Writes one or more bytes |
| `mw ADDRESS WORD ...` | Writes one or more 16-bit words |
| `ml ADDRESS LONG ...` | Writes one or more 32-bit long words |
| `fill ADDRESS LENGTH BYTE` | Writes the same byte to a range of addresses |
| `regs` | Prints the last fault report again |

## The ROM

| Command | What it does |
|----------|--------------------|
| `flashid` | Asks the two ROM chips to identify themselves |
| `flash FILE` | Replaces the BIOS with the one in the file. See appendix D |

## Keys at the prompt

| Key | What it does |
|----------|--------------------|
| [←]{.key} [→]{.key} | Move along the line |
| [Home]{.key} or [Ctrl]{.key} + [A]{.key} | Jump to the start of the line |
| [End]{.key} or [Ctrl]{.key} + [E]{.key} | Jump to the end of the line |
| [Backspace]{.key} | Rub out the character before the cursor |
| [Delete]{.key} or [Ctrl]{.key} + [D]{.key} | Rub out the character under the cursor |
| [Ctrl]{.key} + [K]{.key} | Rub out from the cursor to the end of the line |
| [Ctrl]{.key} + [U]{.key} or [Ctrl]{.key} + [C]{.key} | Rub out the whole line |
| [Ctrl]{.key} + [W]{.key} | Rub out the word before the cursor |
| [↑]{.key} [↓]{.key} | Step through the lines typed earlier |
| [Tab]{.key} | Complete a command, program or file name. A second press lists the choices |

A line can be up to 127 characters long.

## Messages about files

| Message | What it means |
|----------|--------------------|
| `not found` | There is no file or directory of that name |
| `volume not mounted (no card, or not formatted)` | The drive cannot be read. For D:, check the card |
| `bad name (max 63 chars)` | The name is too long or has a character that is not allowed |
| `exists / not empty` | A file of that name is already there, or the directory still has files in it |
| `not a directory` `is a directory` | A file was named where a directory was wanted, or the other way round |
| `no space` | The drive is full |
| `read-only` | The file is marked so that it cannot be changed |
| `too many open files` | A program has more than eight files open at once |
| `not a runnable ELF` | The file is not a program for the Griffin Jr. |
| `does not fit in user RAM` | The program is too large, or was built for a different machine |
| `I/O error` | The drive did not answer properly. For D:, try `sd` |
