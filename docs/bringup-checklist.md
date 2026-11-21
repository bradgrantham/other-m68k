# Griffin Jr. (other-m68k rev C3) bring-up checklist

Printable bench version of `docs/bringup.md`, updated for the rev C3 boards of PCBWay order
T-V19W49409A. One copy per board. Do the stages in order; each assumes the previous ones pass.

Board: ______  Date: ____________

Lines marked **(expected)** are worked out from the firmware and HDL sources and have not been
seen on hardware yet. Write down what really happens.

## A. Before the boards arrive (on the computer)

Everything is built on the Linux machine and copied to the Mac; nothing is built on the Mac.
To rebuild on Linux: `make flash` (firmware), then `make -C hdl fit pins-check svf` (CPLDs).

- [ ] Firmware images on the Mac: `rom_even.bin` and `rom_odd.bin` (512 KB each), from
      `fw/build/` or the `griffin-jr-fw-*.zip` there. Note the version in the zip name; the
      banner in stage G should show it: ________________
- [ ] CPLD images on the Mac: copy `hdl/build/fit/cpld1_mmu.svf` and `cpld2_io.svf` into
      `hdl/build/fit/` of the Mac checkout (the `flash-cpld` targets look there). They are
      made from the JEDs by `make -C hdl svf` (prjbureau `fuseconv`).
- [ ] OpenOCD is installed on the Mac (`openocd --version`).
- [ ] Program the flash chips in the TL866 (minipro device `SST39SF040`):
      `rom_even.bin` into the chip for **U4** (EVEN, D15..D8), `rom_odd.bin` into the chip for
      **U5** (ODD, D7..D0). Label the chips.
- [ ] Erase the four ATF1508AS-10 chips while they are still in the Griffin Rev 1 board, so
      they do not drive Griffin's pinout into this board. (I think an erased ATF1508 leaves
      its I/O pins undriven; I have not checked the datasheet.)
- [ ] FAT32 microSD card with a few files on it (`rom.bin`, a `.BMP`, an `.ELF`).

Bench kit: USB-C wall supply (not a laptop port; the board draws about 1 A), multimeter,
scope or logic analyzer (J9 is 5 V TTL: use the level-shifter board), UM232H/FT232H JTAG
adapter, FTDI serial cable (5 V TTL), TV with composite and S-video inputs, PS/2 keyboard,
Atari joystick, headphones or amplified speaker.

## B. Board as received (nothing fitted, nothing soldered)

- [ ] Visual inspection: bridges, tombstoned parts, polarity of electrolytics, LEDs and diodes,
      pin 1 of U8, U9, U14 and of every socket.
- [ ] J3, J5, J12 and U1 footprints are empty, U11 pads are bare, all sockets are empty.
- [ ] Resistance 5 V to GND (TP3 to TP4) is not a short: ________ ohm
- [ ] Resistance 3.3 V to GND (TP2 to TP4) is not a short: ________ ohm
- [ ] Power from USB-C. PWR LED (D5) lights.
- [ ] TP3 reads 5 V: ________ V   (the supervisor releases reset at 4.5 V)
- [ ] TP2 reads 3.3 V: ________ V
- [ ] Reset line goes high about 150 ms after power, and goes low while SW1 RESET is held.
- [ ] X1 output is 24 MHz.
- [ ] Nothing gets warm. Unplug.

## C. Solder the five hand-fitted parts

- [ ] U11 AD724JRZ (SOIC-16W) first, while the board is flat. Pin 1 matches the silkscreen mark.
- [ ] U1 DIP-64 socket, notch matching the silkscreen (left end).
- [ ] J5 RCA jack (composite).
- [ ] J3 mini-DIN 6 (PS/2).
- [ ] J12 DE-9 male (joystick), boardlocks soldered.
- [ ] Inspect the joints; no bridges on U11.
- [ ] Repeat the two resistance checks from stage B: 5 V ________ ohm, 3.3 V ________ ohm
- [ ] Power up; 5 V and 3.3 V are as before; U11 is not warm. Unplug.

## D. CPLDs alone

JTAG header J7 (ByteBlaster pinout): 1 TCK, 2 GND, 3 TDO, 4 VCC, 5 TMS, 6 GND, 7 n/c, 8 GND,
9 TDI, 10 GND. Chain: header TDI -> U6 (CPLD1, MMU) -> U7 (CPLD2, I/O) -> header TDO. Both
chips must be in their sockets or the chain is open.

- [ ] Fit both ATF1508AS into U6 and U7 (PLCC-84; check the bevelled corner). Nothing else fitted.
- [ ] Power up; current is modest and neither chip gets hot.
- [ ] `make -C hdl jtag-scan`: OpenOCD sees two TAPs, `tap0.tap` and `tap1.tap`, with ID
      0x0150803f (Griffin's settings: UM232H, two `jtag newtap` lines, `-irlen 10`). The
      Makefile runs `sudo openocd`; on the Mac use `OPENOCD=openocd` if sudo is not needed.
- [ ] Program `cpld1_mmu.svf` into U6: `make -C hdl flash-cpld1`. U6 is `tap1` and U7 is
      `tap0` (OpenOCD counts from the TDO end; confirmed on the first board).
- [ ] CPUCLK is 12 MHz (J9 channel 0, or U1 socket pin 15).
- [ ] Program `cpld2_io.svf` into U7: `make -C hdl flash-cpld2`.
- [ ] HSYNC period 63.6 us (J9 channel 18): ________
- [ ] VSYNC period 16.7 ms (J9 channel 19): ________
- [ ] LATCH pulses every 333 ns.
- [ ] TV on J5 (composite) syncs to a black picture.
- [ ] TV on J10 (S-video) syncs to a black picture.

## E. CPU and ROM (no DUART, no RAM)

`fw/test/freerun`, which `docs/bringup.md` mentions, does not exist. The real ROM does the
same job, because the POST uses no RAM and turns LED0 on before it touches anything else.

- [ ] Unplug. Fit the flash chips: EVEN image in U4, ODD image in U5 (silkscreen says which).
- [ ] Fit the MC68000P12 in U1, notch to the notch. All 64 pins in, none folded under.
- [ ] Power up. **LED0 (D3) comes on** and stays on: the CPU left reset, fetched from ROM and
      wrote the GPIO register through the bus buffers. **(expected)** It then stops at the
      first DUART access, because no DUART answers.
- [ ] AS pulses and the address lines toggle after reset (J9 channels 1 and 20-23); BERR
      (channel 6) stays high.
- [ ] RESET button restarts it (LED0 goes off and comes back).

If LED0 stays off: check CPUCLK at U1, RESET and HALT high, ROMOE (J9 channel 12) pulsing,
and that the EVEN/ODD chips are not swapped.

## F. DUART and serial (no RAM)

- [ ] Unplug. Fit the MC68681P in U10.
- [ ] FTDI cable on J2 SERIAL A: pin 1 GND, pin 4 cable TXD, pin 5 cable RXD. Do not connect
      the cable's 5 V. Terminal at **38400 8N1**.
- [ ] Power up. Serial shows `other-m68k POST` and `ROM sum 0000 OK`.
- [ ] **(expected)** With no SRAM it then prints `RAM FAIL @...` and LED0/LED1 alternate at
      about 2 Hz. That is the right result for this stage.

## G. RAM

- [ ] Unplug. Fit both AS6C4008-55PCN in U2 and U3.
- [ ] Power up. Do not press NMI during the POST.
- [ ] LED0, then LED0 + LED1 for about 4 s (RAM test), then LED1, then both off.
- [ ] Serial shows `RAM ... OK` (three dots) and the banner: firmware version, ROM checksum,
      C: size, RAM size, ID `0x6802`. Record the version: ____________
- [ ] After the banner serial A goes quiet: the BIOS hands the port to programs. From here on
      the monitor is on the TV and the PS/2 keyboard.

A failure prints `RAM FAIL @aaaaaaaa exp xxxxxxxx got yyyyyyyy`; `exp 55AA55AA` points at a
stuck WEU or WEL.

## H. Video

- [ ] The banner and the monitor prompt are on the composite output, 40 columns, in colour.
- [ ] The same on S-video.
- [ ] No columns of garbage. If there are, scope the transceiver enable (BUFEN, J9 channel 17)
      and RAMOE (channel 13) phases against the four alignment cases in `docs/architecture.md`.

## I. Keyboard, flash drive, SD card, audio

- [ ] PS/2 keyboard on J3: typing appears; `help` lists the commands.
- [ ] `info` shows sane values, no PS/2 errors and no RX overruns.
- [ ] `led 1`, `led 2`, `led 3`, `led 0` drive LED0 and LED1.
- [ ] `dir C:` lists AUTOEXEC.CMD, README.TXT and the example programs.
- [ ] `flashid` reports the SST39SF040.
- [ ] Insert the microSD card; `sd` prints type, **4-bit** bus width and capacity.
- [ ] `dir D:` lists the card.
- [ ] `beep` gives a 1 kHz tone on J6 AUDIO.
- [ ] `ramtest` passes (it destroys user RAM and both framebuffers).
- [ ] NMI button prints the registers and returns to the monitor.

## J. Programs and the remaining ports

- [ ] `hello` runs from C:.
- [ ] `plasma` runs; ESC or NMI returns to the monitor.
- [ ] `tune` (or `sdplay` with a file on the card) plays audio.
- [ ] Atari joystick on J12: `joytest` shows the four directions and fire.
- [ ] Paddles on J12, if you have a pair: `joytest` shows both moving over the range.
- [ ] J12 pin 7 reads 5 V (through polyfuse F2).
- [ ] `srec` on the keyboard, send an S-record on serial A, `go` runs it.
- [ ] `term` on serial A: typing on the PS/2 keyboard reaches the terminal and back; ESC quits.
- [ ] Serial B (J11): pin 3 reads about 4.7 V (5 V through F3 and D8). No example program
      uses channel B (`term` opens `/dev/ttya`), so its data lines are not tested here.
- [ ] `copy D:X.ELF C:` then run it from C: (writes the flash volume).
- [ ] In-system update: `rom.bin` on the card, `flash D:ROM.BIN`, answer `y`, 32 dots on
      serial A, new banner. Do not press NMI or RESET while it runs; the TL866 recovers a
      failed update.

## K. Soak

- [ ] Supply current with everything running: ________ A (about 1 A expected)
- [ ] 30 minutes of `plasma`; no crash, nothing too hot to touch.
- [ ] Power-cycle five times; it boots to the prompt each time.

## Notes

________________________________________________________________________

________________________________________________________________________

________________________________________________________________________

Sources: `docs/bringup.md`, `fw/README.md` (LED codes, POST output, monitor commands),
`hdl/README.md` and `hdl/STATUS.md` (fits, programming), `kicad/README.md` (J2, J7, J11, J12
pinouts), `kicad/ORDER.md` (parts we fit, power, J9 channels), and Griffin's `cpld/Makefile`
(the OpenOCD and fuseconv commands that are known to work).
