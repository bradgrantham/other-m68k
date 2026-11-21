# Pictures

Everything you see on the screen of the Griffin Jr., text included, is a grid of coloured
squares called pixels, and every pixel is one byte in RAM. There is no separate text screen and no fixed set of
shapes. A program that can write a byte to memory can draw anything.

This chapter explains how that works, using nothing but the monitor's memory commands
from chapter 3.

## One byte, one pixel

The picture is 320 pixels wide and 240 pixels high, which makes 76,800 pixels, and so the
picture takes 76,800 bytes of RAM. The bytes are stored in reading order: the first byte
is the top left pixel, the next is the pixel to its right, and when a row ends the next row
begins straight after it.

![The screen and the addresses of its four corners.](figures/screenmem.svg){width=92%}

Each row is 320 bytes long, which is hex `140`. So to find a pixel, start at `ed400`, add
hex `140` for every row down, and add 1 for every pixel across.

Type `cls`, and then these four lines:

```
mb f6aa0 ff
fill f8300 140 e0
fill f8580 280 1c
fill f8a80 500 03
```

![One white pixel, then one, two and four rows of colour.](shots/rows.png){width=62%}

The `mb` command wrote a single byte and lit a single white pixel near the middle of the
screen. Look closely and you will find it above the stripes. The first `fill` wrote hex
`140` bytes, which is exactly one row, so it drew a line one pixel thick from edge to edge.
The others wrote two rows and four rows.

::: try
Type `fill ed400 12c00 03`. Hex `12c00` is 76,800, so this fills every pixel on the screen
with blue, including the pixels that made up the text. Type `cls` to get the usual
background back.
:::

## How a byte becomes a colour

A byte is eight bits, and the Griffin Jr. shares them out among the three colours a
television mixes: three bits for red, three for green and two for blue.

![The eight bits of a colour byte. The number in each box is what that bit is worth within its own colour.](figures/byte.svg){width=78%}

Red and green can each be set to eight strengths, from 0 for none to 7 for full. Blue
gets only four, from 0 to 3, because the eye is least sensitive to changes in blue and
there were only eight bits to go round. Eight times eight times four gives the 256
colours.

To work out the byte for a colour, multiply the red strength by 32, the green strength
by 4, and add the blue strength:

| Colour | Red | Green | Blue | Sum | Byte in hex |
|--------|----|----|----|----------|------|
| Black | 0 | 0 | 0 | 0 | `00` |
| Bright red | 7 | 0 | 0 | 224 | `e0` |
| Bright green | 0 | 7 | 0 | 28 | `1c` |
| Bright blue | 0 | 0 | 3 | 3 | `03` |
| Yellow | 7 | 7 | 0 | 224 + 28 = 252 | `fc` |
| Orange | 7 | 4 | 0 | 224 + 16 = 240 | `f0` |
| Mid grey | 4 | 4 | 2 | 128 + 16 + 2 = 146 | `92` |
| White | 7 | 7 | 3 | 224 + 28 + 3 = 255 | `ff` |

This is the sum that BASIC's `RGB` does for you. The chart in the appendix shows all 256
colours with their numbers, which is quicker than arithmetic when you only want to pick
one.

## Where the text comes from

When the BIOS prints a letter, it draws the letter's shape into the picture, pixel by pixel,
exactly as your own program might. Each character sits in a box 6 pixels wide and 10 high.
Fifty-three boxes fit across the 320 pixels with two pixels to spare, and twenty-four fit down
the 240, which is where the screen's 53 columns and 24 lines come from.

This explains two things you may have noticed. Text and drawings can share the screen,
because both are only pixels. And scrolling the text takes a moment, because the BIOS has to
move nearly the whole picture up by ten rows, which is about 74,000 bytes each time.

## Two pages

The Griffin Jr. sets aside room for two complete pictures. Page 0 starts at `ed400` and is
the one you normally see. Page 1 starts at `da800`, just below it in memory. A switch in
CPLD 1 chooses which page goes to the television, and the change takes effect between one
frame and the next, so the screen never shows half of each.

The monitor's `page` command works the switch.

::: try
Run `plasma`, let it play for a moment and press a key. Now type `page 1`. The screen
shows the last picture that `plasma` drew on the other page, frozen. The monitor is still
listening, although you cannot see what you type, so type `page 0` to come back.
:::

Programs use the two pages to move things smoothly, as chapters 4 and 6 described: draw
on the page that is hidden, then swap. A program that does not need two pages can use
page 1's 75 KB as ordinary memory.

::: curious
**Sharing the memory with the television**

A television draws its picture one line at a time, 262 lines in each frame and about 60
frames a second, and it cannot wait. While it sweeps across a line, the Griffin Jr. has
to fetch the bytes for that line from RAM at a steady rate of one byte for every pixel.

In many home computers of the 1980s the processor had to stop and wait whenever the
picture needed the memory, and some lost a large part of their speed that way. The
Griffin Jr. does it differently. Time is cut into slots of a third of a millionth of a
second. In the first half of every slot the RAM belongs to the picture, and in the second
half it belongs to the processor. The two bus buffer chips from chapter 1 are the gates
that keep the halves apart.

The processor never has to stop, so a program runs at the same speed whether the
television is drawing the top of the screen, the bottom, or nothing at all. That steadiness
is what lets a program count on having exactly the same time for every line, which matters
a great deal for sound, as the next chapter shows.
:::

::: curious
**Sixty times a second**

Each time the television finishes a frame and goes back to the top, CPLD 2 sends the
processor a signal. The BIOS uses it to count the `ticks` you met in chapter 3 and to keep
the clock. Programs use it as a steady beat: BASIC's `VSYNC` waits for it, and so does
every game that wants to move at an even speed.

A frame lasts for 262 lines of 764 clock ticks each. That is 200,168 ticks of the 12 MHz
clock, which comes to 59.95 frames a second and not quite 60.
:::
