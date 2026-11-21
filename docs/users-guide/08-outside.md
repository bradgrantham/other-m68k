# The outside world

This chapter goes round the edge of the board and looks at each socket in turn: what
plugs into it, what the pins carry, and which commands and programs use it.

## The SD card

The microSD card is drive D:, and for most purposes that is all you need to know about it.
You use it with the file commands from chapter 3.

The card must be formatted as FAT32, or as the older FAT16 that small cards use. Cards of
32 GB and under are sold as FAT32. Larger cards are sold in a format called exFAT, which
the Griffin Jr. cannot read, so a large card has to be reformatted as FAT32 on another
computer before you use it.

Put the card in before you connect the power and leave it there until you have
disconnected it. The socket has no way to tell the Griffin Jr. that a card has gone.

The `sd` command asks the card to describe itself.

```{.screen session=sdinfo}
```

The first two lines are the useful ones: the kind of card and how much it holds. The rest
are the card's own records of who made it and when. If a card is giving trouble, `sd` is
the first thing to try, because it starts the card again from the beginning. If `sd`
cannot find the card at all, check that it is pushed fully in.

::: curious
**Why the card is slower here than in a camera**

An SD card expects to be driven by a chip built for the purpose, which can move millions
of bytes a second. The Griffin Jr. has no such chip. The card's six wires go to CPLD 2,
and the 68000 works them one at a time by program, raising and lowering the clock wire
itself for every four bits that cross.

Done that way, a block of 512 bytes takes about a two-hundredth of a second to read, and
the card moves about 100 KB a second. That is why a picture of 230 KB needs more than two
seconds just to be read.
:::

## The joystick port

The nine-pin port takes the controllers made for the Atari 2600 and for the Atari,
Commodore and Amiga home computers, which all use the same plug and the same pins.

![The joystick port, looking into the socket on the board.](figures/joyport.svg){width=55%}

| Pin | Joystick | Paddles |
|----|----------|--------------------|
| 1 | Up | |
| 2 | Down | |
| 3 | Left | Button of paddle B |
| 4 | Right | Button of paddle A |
| 5 | | Knob of paddle B |
| 6 | Fire button | |
| 7 | 5 volts | 5 volts |
| 8 | Ground | Ground |
| 9 | | Knob of paddle A |

A joystick is five switches. Each one connects its pin to ground when it is pressed, and
the Griffin Jr. reads the five pins directly.

A pair of paddles is two knobs and two buttons on one plug. The buttons arrive on the
pins a joystick uses for left and right, which is why `joytest` shows them as `L` and `R`.

Pin 7 supplies 5 volts for controllers that need it, through a fuse that resets itself and
limits the current to 200 milliamps. If you build a controller of your own, the five
switch pins are the easy place to start: anything that connects a pin to ground counts as
a press.

Use `joytest` from chapter 4 to check a controller, and `JOY` and `PADDLE` from chapter 5
to use one in a program.

## The serial ports

A serial port sends text and data down a wire one bit at a time. It is the oldest and
simplest way to connect two computers, and the Griffin Jr. has two, marked SERIAL A and
SERIAL B on the board.

Each is a row of six pins, laid out to take the common USB serial cable known as an FTDI
cable. The other end of the cable goes into a USB socket on a modern computer, where the
Griffin Jr. appears as a serial device.

![A serial header. The names are those used on an FTDI cable.](figures/serial.svg){width=50%}

Pin 1 is ground, which is the black wire on an FTDI cable. Line the cable up with that and
the rest follow.

The settings never change: 38400 baud, 8 data bits, no parity and 1 stop bit, often
written `38400 8N1`. Baud is the number of bits sent each second, so 38400 baud moves
about 3,800 characters a second. Set the program at the other end to the same.

::: careful
The pins work at 5 volts. Use a USB serial cable sold as "5 V TTL", or one that says it
accepts 5 volt signals. Never connect these pins straight to the nine-pin serial port on
an old PC or modem. Those ports use much higher voltages, of both signs, and they can
damage the Griffin Jr.
:::

### Serial port A

Port A is the general-purpose port. Only three of its pins are connected: ground, and one
wire in each direction.

At start-up the BIOS sends the self-test report from chapter 2 out of this port. After
that the BIOS does not touch it, and it belongs to whatever program wants it:

- **`term`** makes the Griffin Jr. a terminal for the computer at the other end.
- **`basic -s`** runs BASIC with the serial port as its keyboard and screen, so that you
  can use BASIC on the Griffin Jr. from a window on another computer. To finish, type
  [Ctrl]{.key} + [D]{.key} at the far end.
- **`sstv`** watches the port while it is sending, and stops if an [ESC]{.key} arrives.
- **`srec`**, a monitor command, receives a program sent from another computer. Chapter
  10 describes it.

To watch the self-test report, connect the cable, open a terminal program on the other
computer at 38400 8N1, and press RESET on the Griffin Jr.

### Serial port B

Port B is meant for a device. The BIOS leaves it entirely alone.

Unlike port A, it supplies power: pin 3 carries 5 volts, limited to 200 milliamps, so a
small gadget can run from the port with no supply of its own. Pin 6 is an output that a
program can raise and lower. Together these are what an old serial mouse needs, by way of
a small adapter, and that is the use the port was designed around.

## Video and audio

The yellow VIDEO socket and the S-VIDEO socket carry the same picture at the same time,
so you can connect both if you wish, for example one to a television and one to a video
recorder.

Composite video sends the whole picture down one wire, with the colour folded in on top
of the brightness. A television has to pull the two apart again and never quite manages,
which is why fine detail on a composite picture shows fringes of false colour. S-video
keeps brightness and colour on separate wires. If your set has the input, the text will be
noticeably sharper.

Both outputs follow the NTSC standard. The audio socket is described in chapter 7.

## Power

The USB-C socket takes in 5 volts and nothing else. It does not carry data, so plugging
the Griffin Jr. into a computer powers it but does not connect the two.

The board draws up to about 1 amp. A fuse that resets itself protects the board and your
power supply if something goes wrong, and a small supervisor chip holds the processor in
reset until the voltage is high enough to run on. If the red light is on but the machine
keeps restarting, the supply or the cable is too weak. Try a shorter cable or a stronger
supply.
