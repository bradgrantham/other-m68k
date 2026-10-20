# Video encoder: AD724 application notes for this board

Source: AD724 datasheet Rev. B (`~/.local/share/datasheets/AD724.pdf`). This is what the
schematic generator implements for the video output section.

## Pinout (16-lead wide SOIC, R-16)

| pin | name | our connection |
|---|---|---|
| 1 | STND | NTSC/PAL select: HIGH = NTSC, LOW = PAL. Rev B board: 10 k pull-up (NTSC only, no jumper); CPLD2 PALJMP is pulled low separately |
| 2 | AGND | ground plane |
| 3 | FIN | parallel-resonant crystal to AGND: 3.579545 MHz (NTSC build) or 4.433619 MHz (PAL build), with a ~10 pF shunt footprint (datasheet found ~10 pF across the crystal correct on its eval board; 10-30 pF range) |
| 4 | APOS | +5V analog, 0.1 uF + 10 uF |
| 5 | ENCD | tie HIGH through 10 k (encode enabled) |
| 6,7,8 | RIN, GIN, BIN | from the RGB resistor DACs through 0.1 uF AC-coupling caps, each input terminated with 75 ohm to ground at the pin |
| 9 | CRMA | unused (chroma); leave open or pad for S-video |
| 10 | COMP | composite out: 220 uF series + 75 ohm series to the RCA jack |
| 11 | LUMA | unused (luma/S-video); pad optional |
| 12 | SELECT | LOW = FSC mode (crystal or 1x subcarrier clock). Tie LOW via 10 k |
| 13 | DGND | ground plane |
| 14 | DPOS | +5V digital, 0.1 uF + 10 uF |
| 15 | VSYNC | from CPLD1 VSYNC (active-low pulse); or tie HIGH and feed composite sync to HSYNC |
| 16 | HSYNC | from CPLD1 HSYNC (active-low pulse) |

Logic inputs are CMOS/TTL compatible (VIL < 1.0 V, VIH > 2.0 V), fine for 5 V CPLD outputs.
HSYNC and VSYNC are XNORed internally into CSYNC; burst is inserted after each valid
horizontal sync. Equalisation and serration pulses are not required for consumer displays.

## RGB inputs from the CPLD pixel register

Full scale is 714 mV p-p into the 75 ohm termination. Each colour is a binary-weighted
resistor summer driven by CPLD2's 5 V CMOS outputs into that 75 ohm load:

    Vout = sum_i Vi * (Rp / Ri),  Rp = 75 || R0 || R1 || R2  (exactly linear in the bits)

Full scale 0.714 V with Vi = 5 V needs the parallel combination of the bit resistors to be
450 ohm. With 1:2:4 weights (R, 2R, 4R) that gives R = 787 ohm:

| colour | bits | resistors (MSB..LSB), E96 | E24 alternative |
|---|---|---|---|
| red | 3 | 787, 1.58k, 3.16k | 820, 1.6k, 3.3k (0.69 V FS) |
| green | 3 | 787, 1.58k, 3.16k | 820, 1.6k, 3.3k |
| blue | 2 | 681, 1.37k | 680, 1.3k (0.72 V FS) |

The CPLD driver's ~40 ohm output impedance and a VOH below 5 V lower full scale by roughly
5-10%; the AD724 tolerates a +-15% gain error, and consumer TVs more than that. Use 1%
resistors so the three channels match (otherwise white has a tint).

## Clock

FSC mode with a crystal: the subcarrier is independent of the 24 MHz system clock, so the
colour is asynchronous to pixels (slight dot-crawl jitter, acceptable; the datasheet
discusses it). Nothing else on the board may use the FIN oscillator.

## Output

COMP -> 220 uF (electrolytic, + towards the AD724) -> 75 ohm -> RCA centre. The output
level is 2x normal because the far-end 75 ohm halves it. Keep the 75 ohm terminations and
coupling caps close to the chip; AGND and DGND both to the ground plane with short paths.

## Power

APOS and DPOS from 5V via a ferrite bead for APOS (optional but cheap), 0.1 uF ceramic at
each pin plus 10 uF bulk. Quiescent current 33 mA typical, 42 mA max.
