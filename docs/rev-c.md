# Rev C ideas (collected after the rev B4 order, 2026-10-20)

Nothing here is designed yet; these are the agreed directions for the next board.

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
