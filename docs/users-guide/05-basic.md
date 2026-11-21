# BASIC

BASIC is a programming language made for people who are not programmers. It was written
in 1964 so that students could use a computer without first learning how one works, and
by the 1980s nearly every home computer came with it. Its statements are ordinary English
words such as `PRINT`, `IF` and `GOTO`, and it lets you try one line at a time and see
the result at once.

The Griffin Jr. has its own version, Griffin BASIC, with statements for drawing, for
sound and for the joystick. This chapter starts from nothing and ends with a small game.

## Starting and stopping

Type `basic` at the monitor's prompt.

```{.screen session=b_direct}
```

BASIC announces itself and waits. It has no prompt of its own. When it has finished
something it says `OK`, and then it is your turn again.

The session above shows the simplest way to use it. A line without a number in front is
carried out the moment you press [Enter]{.key}, which makes BASIC a very good calculator.
`quit` takes you back to the monitor.

A few things are worth noticing in it:

- `PRINT` shows whatever follows it. Text goes in double quotes, and sums are worked out
  before they are shown. The signs are `+` and `-`, `*` for times and `/` for divide.
- A comma between two things moves the second one across to the next column. A
  semicolon puts it right after the first.
- `friend$ = "Griffin"` stores a piece of text under the name `friend$`. A name like
  this is called a **variable**. Names that end in `$` hold text and names without it hold
  numbers.
- Capital and small letters are the same to BASIC, except inside quotes.

A variable's name can be as long as you like, which lets a program read almost like a
sentence: `score = score + 10`. The one rule is that a name must not be one of BASIC's
own words, so `step` and `play` are not allowed, while `steps` and `player` are fine.

## Your first program

A line that starts with a number is not carried out. BASIC stores it instead, and the
stored lines together are a **program**. `RUN` carries them out in the order of their
numbers.

```{.screen session=b_first}
```

`FOR n = 1 TO 5` and `NEXT n` mark out a loop. Everything between them is done five times,
with `n` counting from 1 to 5.

The numbers go up in tens for a reason. To change a line, type it again with the same
number. To put a new line between two others, give it a number in between. `LIST` shows
the program as it now stands.

```{.screen session=b_edit}
```

The new line 20 replaced the old one, and line 5 went in ahead of line 10 even though it
was typed last.

A few more words finish the set you need for working on a program:

| You type | What happens |
|----------|--------------------|
| `RUN` | Carries out the program |
| `LIST` | Shows the program. `LIST 100-200` shows part of it |
| `NEW` | Throws the program away so that you can start another |
| A line number by itself | Removes that line |
| [Ctrl]{.key} + [C]{.key} | Stops a program that is running |
| `CONT` | Carries on after a stop |
| `QUIT` | Leaves BASIC |

## Keeping a program

A program lives in RAM, so it is gone when you leave BASIC or switch off. `SAVE` writes it
to a file and `LOAD` brings it back.

```{.screen session=b_save}
```

`NEW` threw the program away, as the empty `LIST` shows, and `LOAD` brought it back from
the card.

Give your programs names ending in `.BAS`. You can also run a saved program straight from
the monitor by putting its name after `basic`, in which case BASIC runs it and returns to
the monitor when it ends:

```
basic D:SQUARES.BAS
```

A `.BAS` file is plain text. That means you can also write a program on another computer,
copy it to the SD card and run it on the Griffin Jr.

## Asking questions and making choices

Programs become interesting when they react to the person using them. This one is a
guessing game, and it uses three new statements.

```{.listing file=card/GUESS.BAS}
```

```{.screen session=b_guess}
```

`INPUT` prints its question, waits for you to type something and press [Enter]{.key}, and
stores what you typed in the variable named at the end.

`IF` tests something and carries out the rest of its line only when the test is true. The
tests are `=`, `<`, `>`, `<=`, `>=` and `<>` for "not equal", and you can join two tests
with `AND` or `OR`. If you add `ELSE`, what follows it is carried out when the test is
false.

`GOTO` jumps to another line. Lines 80 and 90 use it to go back and ask again.

`RND(1)` gives a different number between 0 and 1 each time it is used, and line 30
stretches that into a whole number from 1 to 100. `RANDOMIZE` shuffles the supply of
numbers, so that the game is not the same every time you run it. `REM` begins a remark,
which BASIC ignores. It is there for the person reading the program.

## Drawing

The screen is 320 dots wide and 240 dots high. Every drawing statement says where to draw
by giving two numbers: how far across from the left edge, and how far down from the top.
So `0, 0` is the top left corner, `319, 239` is the bottom right, and `160, 120` is the
middle.

```{.listing file=card/SCENE.BAS}
```

![What `SCENE.BAS` draws.](shots/b_scene.png){width=62%}

The drawing statements are these. In each one the colour comes last and can be left out,
in which case BASIC uses the colour set by the last `COLOR` statement.

| Statement | What it draws |
|----------|--------------------|
| `CLS colour` | Fills the whole screen with one colour |
| `PSET x, y, colour` | One dot |
| `LINE x1, y1, x2, y2, colour` | A line from one point to another |
| `RECT x, y, wide, high, colour` | The outline of a rectangle. `x, y` is its top left corner |
| `FRECT x, y, wide, high, colour` | A filled rectangle |
| `CIRCLE x, y, radius, colour` | The outline of a circle. `x, y` is its centre |
| `FCIRCLE x, y, radius, colour` | A filled circle |
| `GPRINT x, y, text$, colour` | Text on the picture |

A colour is a number from 0 to 255, where 0 is black and 255 is white. The easy way to
get the number for a colour is `RGB`, which takes the amounts of red, green and blue you
want, each from 0 to 255, and gives back the nearest colour the Griffin Jr. has. So
`RGB(255, 0, 0)` is red and `RGB(255, 230, 0)` is the yellow of the sun above. Chapter 6
explains what is inside the number.

The roof in the scene shows a useful trick. BASIC has no statement for a filled triangle,
so lines 60 to 80 draw the roof as 41 lines, one above another, each a little shorter
than the one below.

While a picture is showing you cannot see the text screen. When a program that has drawn
a picture comes to its end, the picture stays until you press a key, and then the text
comes back. A program can also bring the text back itself with `TEXT`.

## Movement

Things move on a screen the way they do in a film: the program draws the thing, waits a
moment, rubs it out and draws it again a little further on.

```{.listing file=card/BALL.BAS}
```

![`BALL.BAS`, with the ball on its way to the bottom edge.](shots/b_ball.png){width=62%}

Line 40 rubs the ball out by drawing it again in black. Line 50 moves it, by adding `dx`
to how far across it is and `dy` to how far down. Lines 60 and 70 watch for the edges of
the screen and turn the ball round by changing the sign of `dx` or `dy`. Line 80 draws
the ball in its new place.

`VSYNC` in line 90 waits until the television has finished drawing the current picture.
That happens 60 times a second, so the loop runs at a steady 60 steps a second whatever
else is going on, and the ball crosses the screen at the same speed every time.

`INKEY$` in line 100 is how a program checks the keyboard without stopping. It gives the
key that was pressed, or an empty piece of text if none was. Here the loop carries on for
as long as it comes back empty.

::: try
Change the numbers in line 30 to make the ball faster or slower. Then take out line 40
and run the program again: with nothing to rub it out, the ball leaves a trail.
:::

::: curious
**Smoother movement with two pages**

A ball that is rubbed out and drawn again can flicker, because now and then the
television catches the moment when it is not there. The cure is the one `plasma` uses in
chapter 4. `PAGE draw, show` chooses which of the two pages your statements draw on and
which one the television shows. Draw each new picture on the hidden page, then swap them
with one `PAGE` statement.

`SPRITE DEFINE` goes further. It takes a small piece of the picture and remembers it under
a number, and `SPRITE` then stamps that piece anywhere you like, which is much faster than
drawing it from circles and lines each time. Up to 32 sprites can be kept at once.
:::

## Sound

BASIC has three voices, so three notes can sound together.

`BEEP` makes a short tone. `SOUND` plays one note and gives you control over it:

```
SOUND 440, 500
```

The first number is the pitch in hertz, which is vibrations a second, and the second is
how long the note lasts in thousandths of a second. This one is the A that orchestras
tune to, for half a second. Two more numbers can follow: the volume from 0 to 15, and
which voice to use from 1 to 3.

A program does not wait for a sound to finish. The note is put in a queue and plays while
the program carries on, which is what a game needs. That is why `BALL.BAS` can make a
noise at each bounce without the ball pausing.

For tunes, `PLAY` is easier. It takes the music written out as text, one piece of text
for each voice.

```{.listing file=card/SONG.BAS}
```

```{.screen session=b_song}
```

The music is the round "Frère Jacques". Lines 20 to 40 hold the tune in three parts. Line
60 plays it on two voices at once: the first voice starts straight away, and the second,
an octave lower, begins with two bars of rest (`R1 R1`) so that it comes in behind the
first.

Line 80 is there because sound plays in the background. `PLAYING(0)` is true for as long
as any voice still has notes to play, so this line keeps the program alive until the music
has finished.

The letters in a `PLAY` text mean this:

| In the text | Meaning |
|----------|--------------------|
| `C` `D` `E` `F` `G` `A` `B` | A note. Add `#` for a sharp or `-` for a flat |
| A number after a note | Its length: `C4` is a quarter note, `C2` a half note, `C8` an eighth |
| `R` | A rest, with a length in the same way |
| `L4` | The length for the notes that follow and have no number of their own |
| `O4` | The octave, from 1 to 8. The A in octave 4 is 440 hertz |
| `>` `<` | Up one octave, or down one |
| `T120` | The tempo, in quarter notes a minute |
| `V10` | The volume, from 0 to 15 |

## The joystick and paddles

`JOY` reads the joystick. `JOY(0)` tells you about left and right: it is -1 when the stick
is pushed left, 1 when it is pushed right and 0 when it is in the middle. `JOY(1)` does
the same for up and down, with down as 1. `JOY(2)` is true while the fire button is held.

Because the answers are -1, 0 and 1, you can add them straight on to a position. This
program is a drawing toy built on that idea.

```{.listing file=card/SKETCH.BAS}
```

![`SKETCH.BAS` after the stick has been held down and to the right.](shots/b_sketch.png){width=62%}

Line 40 moves the pen by whatever the joystick says. Lines 50 to 80 keep it on the screen.
Line 90 picks a new colour at random whenever the button is pressed.

`PADDLE(0)` and `PADDLE(1)` read the two paddles. Each gives a number from 0 to 255 as the
knob turns, or -1 if no paddle is plugged in.

## A game: CATCH

Here is everything from this chapter working together. Stars fall from the top of the
screen and you catch them in a basket. Every star you catch makes the next one fall
faster, and the game ends when you have missed three.

The basket follows a paddle if one is plugged in, and the joystick if not.

```{.listing file=card/CATCH.BAS}
```

![`CATCH.BAS`, with a star on its way down.](shots/b_catch.png){width=62%}

The program has three parts, and the remarks mark where each begins.

Lines 40 to 90 start a new star. They choose a place for it along the top, set its speed
from the score, and print the score and the number of lives.

Lines 100 to 210 are the game's loop, which goes round once for every picture the
television draws. Each time round it rubs out the basket, finds out where the player
wants it, draws it there, and then does the same for the star. Line 130 is the one that
reads the controller: if `PADDLE(0)` gives a real reading the basket goes where the
paddle points, and otherwise the joystick nudges it left or right.

Lines 220 to 300 deal with a star that has reached the bottom. If it landed outside the
basket, line 230 jumps to the lines that take away a life. If not, line 240 adds one to
the score and goes back for another star.

::: try
The game is short on purpose, so that there is room to make it yours. Give the player
five lives. Make the basket narrower as the score goes up. Let two stars fall at once.
Play a tune with `PLAY` when the game is over.
:::

## When BASIC complains

If BASIC cannot carry out a line, it stops and says why. When the trouble is in a
program, it also gives the number of the line.

```{.screen session=b_errors}
```

| Message | What it usually means |
|----------|--------------------|
| `SYNTAX ERROR` | BASIC could not understand the line. Look for a misspelled word or a missing quote or bracket |
| `TYPE MISMATCH ERROR` | Text was used where a number was wanted, or the other way round |
| `DIVISION BY ZERO ERROR` | A sum tried to divide by nothing |
| `UNDEF'D STATEMENT ERROR` | A `GOTO` points at a line number that does not exist |
| `ILLEGAL QUANTITY ERROR` | A number is outside what the statement allows, such as a dot off the screen |
| `FILE ERROR` | The file is not there, or the drive could not be read or written |

After an error the program is still in memory. Use `LIST` to look at the line, type it
again correctly, and `RUN`.

One mistake is easy to make in Griffin BASIC. Its words must stand apart from the names
around them, so write `GOTO 100` and `FOR n = 1 TO 10` with the spaces. Many old BASICs
let you run everything together, as in `GOTO100` or `FORN=1TO10`, and listings from old
books and magazines are often printed that way.

## Programs from old books

A great many BASIC programs were printed in books and magazines in the 1970s and 1980s,
and most of them were written for Microsoft BASIC, the commonest kind. Griffin BASIC can
run those too. Start it with `-m`, and it becomes a Microsoft-style BASIC that accepts
words run together and keeps to the old rules:

```
basic -m
basic -m D:LUNAR.BAS
```

A program file whose first line is `REM MICROSOFT BASIC` is run this way without the `-m`.
The drawing, sound and joystick statements belong to Griffin BASIC and are not there in
this mode. Remember too that many old programs expect a screen wider than 53 characters,
so their output may wrap.
