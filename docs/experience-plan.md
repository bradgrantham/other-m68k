# The Griffin Jr. experience: software plan (recorded 2026-11-21)

Part of the reason to build a machine like this is the experience of exploring it: what
tools are there, what can the hardware do. This is the list of programs and systems that
should exist so that the machine rewards that exploration. Nothing here is designed yet;
each item gets its own design discussion before it is built.

Already in place: the monitor (line editor, scripts, drives C: and D:), the SDK examples
(`cat`, `ls`, `bmpview`, `plasma`, `sdplay`, `sstv`, `term`, `tune`, `joytest`), and BASIC
with graphics and sound, which lives in its own tree (`../polybasic`, `make griffinjr`
builds `BASIC.ELF`) and is therefore not on this list.

## Making things on the machine

1. **Full-screen text editor.** Nothing can be created on the Griffin without one. A small
   vi- or WordStar-style editor for the 53x24 console; makes `AUTOEXEC.CMD`, scripts and
   BASIC programs editable in place.
2. **Forth.** Interactive access to every register in `HARDWARE.md`: type a word, watch the
   LED or the DUART respond.
3. **Native assembler and debugger.** Line assembler, disassembler, breakpoints, single-step
   through the trace bit, register display; the NMI button breaks into it.
4. **Small C compiler or a Pascal.** A Small-C-class compiler emitting 68000 assembly for
   item 3 would make the machine self-hosting.

## Showing what the hardware can do

5. **Paint program.** RGB332 palette picker, joystick or serial mouse (needs the channel-B
   mouse driver and the MAX232 adapter, both still to do), saves BMPs that `bmpview` and
   `sstv` already read.
6. **Music tracker or soft-synth.** A 3- or 4-voice wavetable player with a pattern editor
   on the PDM audio path (one byte per video line, about 15.7 kHz). A MOD player is the
   stretch goal.
7. **Graphics library and demo.** Page-flipped sprites, scrolling, lines and polygons, text
   over graphics, as an SDK library with a showpiece demo. The paint program and the games
   build on it.
8. **Paddle and joystick games.** A Breakout-style and a two-player Pong-style game for the
   paddles, plus an original joystick game, with the source on C: as worked examples.
9. **System inspector.** Live memory map, DUART state, a logic-probe view of the joystick
   and paddle lines, video and audio timing, a benchmark.

## Systems and connections

10. **Z-machine interpreter.** Infocom-format story files from the SD card.
11. **Operating systems.** Serial A was released to programs for this. Candidates:
    CP/M-68K (a period software library), FUZIX (a real multitasking Unix-like on the
    68000), or a home-grown preemptive kernel on the HSYNC or DUART timer. More than one
    may be worth doing.
12. **File transfer and a real terminal.** XMODEM/ZMODEM or Kermit over `/dev/ttya`, and
    `term` grown to VT100, so files move without the SD-card shuffle and the Griffin can
    serve as a terminal for other machines.

## The user's guide

13. **A user's guide in the style of the manuals of the time.** Fun, with low-level detail,
    written to connect the reader with every capability of the machine: first power-on,
    the monitor, BASIC, pictures and sound, joysticks and paddles, the serial ports, the
    memory map and registers, and a tour of each program above as it arrives. It
    complements `docs/programmers-guide.md`, which stays the reference.

First draft complete 2026-11-21 (`docs/users-guide/`): ten chapters and five appendices
covering the monitor, the programs on C:, BASIC and the hardware. Each program above adds
its section or chapter when it lands.

## Releases

The user's guide PDF is a build product and is not checked in. The aim is for GitHub to
build it, the card package (`make card`: `griffin-jr-card.zip`) and the enclosure files,
and publish them as GitHub releases; the checked-in
enclosure STLs go away when that exists. Not started.

## Suggested order

The user's guide first, describing the system and the programs that exist today (monitor,
SDK examples, BASIC). Then the editor, then the graphics library (the paint program, the
games and the demo sit on it), then the inspector and the debugger. Each program adds its
chapter to the guide when it lands. Nearly made the list: an autorun slideshow or attract mode.
