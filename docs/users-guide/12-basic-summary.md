# Griffin BASIC in brief

Square brackets mark something that can be left out. A colour is a number from 0 to 255.
Where a colour can be left out, BASIC uses the one set by `COLOR`.

## Working on a program

| You type | What happens |
|----------|--------------------|
| `RUN` | Carries out the program |
| `LIST [FROM][-TO]` | Shows the program, or part of it |
| `NEW` | Throws the program away |
| `SAVE "NAME"` | Writes the program to a file |
| `LOAD "NAME"` | Reads a program from a file |
| `CONT` | Carries on after `STOP` or [Ctrl]{.key} + [C]{.key} |
| `QUIT` | Leaves BASIC |

## Statements

| Statement | What it does |
|----------|--------------------|
| `PRINT things` | Shows numbers and text. A comma moves to the next column and a semicolon joins on |
| `INPUT ["question";] name` | Asks for a value from the keyboard |
| `GET name$` | Waits for one key and stores it |
| `name = value` | Stores a value in a variable. `LET` may be written in front |
| `IF test THEN ... [ELSE ...]` | Carries out one thing or the other. A line number alone after `THEN` jumps to that line |
| `FOR name = a TO b [STEP c]` ... `NEXT name` | Repeats the lines between, counting from `a` to `b` |
| `GOTO line` | Jumps to a line |
| `GOSUB line` ... `RETURN` | Jumps to a line and comes back at `RETURN` |
| `ON n GOTO line, line, ...` | Jumps to the first line if `n` is 1, the second if it is 2, and so on. `ON n GOSUB` works the same way |
| `DIM name(size)` | Makes a numbered row of variables. `DIM grid(7, 7)` makes a table |
| `DATA values` / `READ names` / `RESTORE` | Keeps a list of values in the program, reads them in turn, and goes back to the start of the list |
| `DEF FN name(x) = formula` | Makes a function of your own, used as `FN name(3)` |
| `RANDOMIZE` | Shuffles the random numbers |
| `REM anything` | A remark, which BASIC ignores |
| `STOP` / `END` | Stops the program. `CONT` carries on after `STOP` |

Several statements can share a line with colons between them.

## Numbers and text

| Function | What it gives |
|----------|--------------------|
| `ABS(x)` `SGN(x)` `INT(x)` | The size of a number without its sign; its sign as -1, 0 or 1; the whole number at or below it |
| `SQR(x)` `EXP(x)` `LOG(x)` | Square root; e to the power x; the natural logarithm |
| `SIN(x)` `COS(x)` `TAN(x)` `ATN(x)` | Trigonometry, with angles in radians |
| `RND(1)` | A random number from 0 up to, but not including, 1 |
| `LEN(a$)` | The number of characters in a text |
| `LEFT$(a$, n)` `RIGHT$(a$, n)` `MID$(a$, start, n)` | Part of a text |
| `ASC(a$)` `CHR$(n)` | The code number of a character, and the character for a code |
| `VAL(a$)` `STR$(x)` | A text turned into a number, and a number turned into text |
| `a$ + b$` | Two texts joined |

The sums are `+`, `-`, `*`, `/` and `^` for "to the power of". The tests are `=`, `<>`,
`<`, `>`, `<=` and `>=`, joined by `AND`, `OR` and `NOT`.

## Pictures

| Statement or function | What it does |
|----------|--------------------|
| `CLS [colour]` | Clears the page being drawn on |
| `COLOR colour [, background]` | Sets the drawing colour, and the colour `CLS` uses |
| `RGB(red, green, blue)` | The nearest colour to those amounts, each from 0 to 255 |
| `PSET x, y [, colour]` | One pixel |
| `POINT(x, y)` | The colour of a pixel, or -1 if it is off the screen |
| `LINE x1, y1, x2, y2 [, colour]` | A line |
| `RECT x, y, w, h [, colour]` | The outline of a rectangle. `FRECT` fills it |
| `CIRCLE x, y, r [, colour]` | The outline of a circle. `FCIRCLE` fills it |
| `GPRINT x, y, text$ [, colour]` | Text on the picture |
| `SCRW` `SCRH` | The width and height of the screen: 320 and 240 |
| `PAGE draw, show` | Which page (0 or 1) to draw on and which to show |
| `VSYNC [n]` | Waits for the next picture, or for `n` pictures |
| `SPRITE DEFINE n, x, y, w, h [, clear]` | Takes sprite `n` (0 to 31, up to 128 by 128 pixels) from the page. Pixels of the colour `clear` are left out when it is drawn |
| `SPRITE n, x, y` | Draws sprite `n` |
| `TEXT` | Goes back to the text screen |

## Sound, controllers and time

| Statement or function | What it does |
|----------|--------------------|
| `BEEP` | A short tone |
| `SOUND hertz, ms [, volume [, voice]]` | Queues a note. 0 hertz is a rest. Volume is 0 to 15 and voice 1 to 3 |
| `SOUND OFF` | Stops all sound at once |
| `PLAY a$ [, b$ [, c$]]` | Queues music, one text for each voice. Chapter 5 lists what goes in the text |
| `PLAYING(n)` | True while voice `n` has sound waiting. `PLAYING(0)` asks about any voice |
| `INKEY$` | A key if one is waiting, or an empty text |
| `JOY(0)` `JOY(1)` | The joystick: left to right as -1, 0, 1, and up to down as -1, 0, 1 |
| `JOY(2)` `JOY(3)` | The fire button; all five switches as one number |
| `PADDLE(0)` `PADDLE(1)` | A paddle, 0 to 255, or -1 if there is none |
| `TIMER` | Seconds since midnight |
| `DATE$` `TIME$` | The date and the time as text |

## Error messages

| Message | What it usually means |
|----------|--------------------|
| `SYNTAX` | BASIC could not understand the line |
| `TYPE MISMATCH` | Text where a number was wanted, or the other way round |
| `ILLEGAL QUANTITY` | A number outside what the statement allows |
| `DIVISION BY ZERO` | A sum divided by nothing |
| `OVERFLOW` | A number too large to hold |
| `UNDEF'D STATEMENT` | A jump to a line that does not exist |
| `UNDEF'D FUNCTION` | `FN` used with a name that no `DEF FN` has set up |
| `NEXT WITHOUT FOR` `FOR WITHOUT NEXT` | The two halves of a loop do not match |
| `RETURN WITHOUT GOSUB` | A `RETURN` was reached without a `GOSUB` |
| `OUT OF DATA` | `READ` has used up every `DATA` value |
| `BAD SUBSCRIPT` | A number in brackets is outside the size given in `DIM` |
| `REDIM'D ARRAY` | `DIM` was used twice for the same name |
| `OUT OF MEMORY` | The program and its variables no longer fit |
| `STRING TOO LONG` | A text grew beyond the longest BASIC allows |
| `CAN'T CONTINUE` | `CONT` was typed with nothing to carry on from |
| `FILE` | A file could not be read or written |
