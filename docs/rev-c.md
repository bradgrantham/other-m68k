# Rev C ideas (collected after the rev B4 order, 2026-10-20)

Nothing here is designed yet. Decision 2026-10-21: **stay with composite video at 320
pixels**; VGA was considered (section at the end, kept for the record) and dropped as
the wrong class of output for this machine.

## Audio: 8-bit PDM shifter in CPLD2 (feasibility checked 2026-10-21, fits)

Today GPIO.AUDIO is one flip-flop toggled by software, so sampled sound is 1 bit at the
line rate (15.7 kHz) and costs the CPU most of a line per sample (the HSYNC ISR with a C
callback measured ~470 of the 762 clocks per line in the emulator). The rev C change:
a write-only 8-bit AUDIO register that CPLD2 shifts out MSB first, one bit every 24 bus
slots (192 of the 1528 clk24 per line, so 8 bits per line = 126 kHz PDM), resynchronised
to HSYNC so the error never accumulates. The byte rotates rather than shifting in zeros,
so a late or missed ISR repeats the previous byte instead of dropping to silence. The CPU
cost is unchanged (one byte write per line) while the output becomes a 126 kHz 1-bit
stream, enough for 8-bit samples at 15.7 kHz with the existing RC low-pass.

Fitter result against the rev 4 CPLD2 source with the board's pin assignments kept
(`-preassign keep`), byte offset 10 used for the register, pin 25 still the audio output:

| | rev 4 CPLD2 | with PDM shifter |
|---|---|---|
| macrocells | 107/128 | 120/128 |
| flip-flops | 73 | 85 |
| nodes + foldbacks | 107/128 | 127/128 |
| I/O pins | 60/64 | 60/64 |

It fits, but at 99 % of the nodes: nothing else of substance can be added to CPLD2 with
it, and the holding latch that would let the ISR write at any point in the line (another
8 flip-flops) does not fit. Without the latch the write replaces the shift register at
once, so the ISR should write early in the line (it runs from the HSYNC interrupt, which
it does naturally; the bit boundary jitters by the interrupt latency, once per line).
If the gpio AUDIO bit must keep working for old software, a mux adds one product term.
The Verilog delta is in the "PDM shifter" snippet below; the variant source lives
outside the repo until rev C work starts.

```verilog
reg [7:0] aud_sh;    // PDM shifter, MSB is the pin
reg [4:0] aud_cnt;   // slots since the last shift (24 slots = 1/8 line)
wire sel_audio = (a[3:1] == 3'd5);          // byte offset 10
assign audio = aud_sh[7];
...
if (wr_en && sel_audio) aud_sh <= d[7:0];
if (hs_rise) aud_cnt <= 5'd0;
else if (e0) begin
    if (aud_cnt == 5'd23) begin
        aud_cnt <= 5'd0;
        if (!(wr_en & sel_audio)) aud_sh <= {aud_sh[6:0], aud_sh[7]};
    end else aud_cnt <= aud_cnt + 5'd1;
end
```

Also worth doing in the same rev, for the ISR cost: clear the HSYNC flag automatically on
the level-4 interrupt acknowledge (saves the INTSTAT write, ~20 clocks) and keep audio in
its own register so no GPIO shadow is needed. With those a hand-written ISR is about
150 clocks per line (20 % of the CPU) instead of 470.

Audio low-pass: DONE 2026-10-21 in board rev C1, the 1 k / 10 nF RC (16 kHz corner, which
passed the sigma-delta hiss above 8 kHz) became 1 k / 33 nF (4.8 kHz; about 6 dB less noise
above 8 kHz in the emulator reconstruction of the tune example, notes unaffected).

## Composite video

## Video outputs

- **S-video**: the AD724's LUMA (pin 11) and CRMA (pin 9) outputs through 75 ohm to a
  mini-DIN 4 jack. Two resistors and a connector; no CPLD or firmware change. Kills
  cross-colour and gives the TV the full luma bandwidth.
- **Monochrome switch**: an analog SPDT video switch (5 V, low on-resistance, e.g. a
  TS5A3159-class part) selecting COMP or LUMA onto the RCA jack, driven by a CPLD2 soft
  switch. Luma alone has sync but no burst, so the TV's colour killer engages: no
  cross-colour, no dot crawl, full luma bandwidth, on any composite input. Forcing grey
  pixels is not enough because the burst would still be there.
- **Component (YPbPr)**: not planned. Needs an RGB-to-YPbPr matrix with three 75 ohm
  drivers; S-video already gives the crisp picture. An RGB+sync header off the DAC
  outputs is cheap if a SCART/RGB monitor is ever wanted.

## Clocking: lock the pixels to the colour subcarrier

Replace the 24 MHz oscillator with 28.636 MHz (8 x Fsc) and run the AD724 in 4FSC mode
from a 14.318 MHz clock out of CPLD1 instead of its own crystal. Then:

- the line is 910 pixel clocks = exactly 227.5 subcarrier cycles (the broadcast value;
  rev B has 227.9), so TV comb filters work and artefact patterns are stable;
- pixel clocks are 7.159 MHz (2 pixels per Fsc cycle, 320 wide) or 14.318 MHz (4 per
  cycle, 640 wide), the CGA / Apple II / NES relationship, so single-pixel patterns map
  to fixed artefact colours instead of a drifting rainbow;
- the CPU runs at 14.318 MHz, which needs a 16 MHz-rated part: MC68HC000-16 (PLCC-68
  certainly; DIP-64 to be confirmed). The rev B MC68000P12 cannot do it. 55 ns SRAM is
  fine; the 70 ns flash is marginal in the 105 ns read window and may need a wait state
  (CPLD1 already generates DTACK, so this is cheap).

## Video modes

The bus gives video one SRAM word per slot (slot = 8 fast clocks = 4 pixels at 14.318
MHz), so the modes that fit without touching the CPU/video time split are:

| mode | pixel clock | bits/pixel | bytes/line | notes |
|---|---|---|---|---|
| 320x240 | 7.159 MHz | 8 (RGB332) | 320 | as rev B; 2 px per Fsc cycle |
| 640x240 | 14.318 MHz | 4 | 320 | fixed 16-colour RGBI mapping (a programmable palette is 128 flip-flops and does not fit CPLD2) |
| 640x240 | 14.318 MHz | 1 | 80 | fg/bg colour registers; 80x30 text with the existing 8x8 font; CPLD1 fetches every 4th slot |

640x240x8 is impossible on this bus (it would need every slot for video). CPLD2 has 21
spare macrocells and 4 spare pins in rev B4; the nibble/bit shifter and mode bits should
fit, a palette does not.

## Readability expectations

Single-pixel strokes at 14.318 MHz are 7 MHz luma detail. The AD724 luma filter and a
TV's Y path (5-6 MHz on S-video, 3-4 MHz on composite with the chroma notch) will soften
them. 80-column text will be readable on S-video and in the monochrome mode, and as poor
as CGA-on-a-TV on colour composite. A font with 2-pixel strokes is worse on colour
composite (3.58 MHz detail sits exactly on the subcarrier).

## SD card: 4-bit data lines (DONE 2026-10-21: spec rev 7, board rev C1)

Implemented the same day: spec rev 7 makes SDIO six open-drain lines with read-back and drops
USERIO; CPLD2 fits at 123/128 macrocells (two nodes spare); board rev C1 in kicad/release has the
dividers removed, six 10 k pull-ups to 3.3 V, DAT1/DAT2 on CPLD2 pins 16/58, J8 pins 5/7/9/11
unconnected; the emulator models a native-mode SDHC/SDSC card. The notes below are the analysis
that led there.

The B4 board has 10 k pull-ups on DAT1/DAT2 but no trace from them to CPLD2, and the CMD
line (MOSI) goes through the 1.8k/3.3k 5 V-to-3.3 V divider, which is wrong once the card
answers on CMD (SD native mode): the divider pulls the idle level low and the CPLD cannot read
the line back. So native 4-bit mode is a rev C item: route DAT1/DAT2 to two of CPLD2's free
pins (16, 58, 60, 61 after rev 6), make CMD and DAT0..3 open-drain from the 5 V CPLD with the
3.3 V pull-ups, and read all five back.

Logic: the rev 6 CPLD2 is at 119/128 macrocells but 127/128 logic nodes. Adding just DAT1/DAT2
as open-drain register bits with read-back does NOT fit (137/128 nodes). Dropping USERIO (the
four expansion-header GPIO bits) makes it fit at 115/128 macrocells, 120/128 nodes. That is
the trade: 4-bit SD costs the user I/O pins.

Payoff: bit-banging 4 bits per clock pulse cuts the SD read loop from 8 to 2 pulses per byte,
roughly 3x (about 100 KB/s, 5 ms per 512-byte block, SD at ~16 % of the CPU for 15.7 kB/s
streaming instead of ~52 %), so full-rate PCM from the card would stream comfortably. The
firmware would need the SD native protocol (CRC7 on commands, a different initialisation,
ACMD6 for bus width, four CRC16s on writes; read CRCs can be ignored). A real SPI engine
(~14 macrocells) does not fit even with USERIO gone.

## VGA (considered and dropped 2026-10-21)

Drop the AD724, its crystal and trim capacitor, and the RCA jack. CPLD2's RGB bits drive
three resistor ladders into 75 ohm (0.7 V full scale); HSYNC/VSYNC go to the HD-15 as
TTL straight from CPLD1. Fewer parts than composite, no artefacts, and every monitor or
cheap VGA-to-HDMI adapter accepts 640x480 at 60 Hz.

Clocking: a 25.000 MHz oscillator (640x480 at 59.5 Hz; monitors lock to it) keeps the
CPU at exactly 12.5 MHz, the MC68000P12's rating. 25.175 MHz would put the P12 0.7 %
over spec. A slot stays 8 fast clocks = 318 ns = 8 VGA pixels. Line doubling needs no
line buffer: the video address counter simply repeats each framebuffer line, so the
fetch rate per output line is unchanged.

Modes with the present one-word-per-slot bus (16 bits per 8 pixels):

| mode | bits/pixel | colours | bytes | notes |
|---|---|---|---|---|
| 640x480 | 1 | fg/bg registers | 38400 | 80x60 text with the 8x8 font, or 80x30 line-doubled |
| 640x480 | 2 | 4 fixed | 76800 | |
| 320x240 doubled | 4 | 16 fixed (RGBI) | 38400 | |
| 160x120 quadrupled | 8 | RGB332 | 19200 | |

Keeping **320x240x8** (the rev B mode) needs two video words per slot: either a
12-fast-clock slot (CPU RAM access every 6 CPU clocks instead of 4, about 30 % slower
for code in RAM, code in ROM unaffected) or a tighter 8-clock schedule with three SRAM
accesses (feasible with 55 ns SRAM, about 106 ns each, but little margin). Decide this
first; it shapes CPLD1.

A programmable palette still does not fit the CPLDs (16 x 8 bits = 128 flip-flops);
fixed RGBI mappings do.
