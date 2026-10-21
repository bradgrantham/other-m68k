#!/usr/bin/env python3
"""pdmtab.py OUT.bin -- the BIOS PDM encoder table (spec rev 5 audio).

First-order sigma-delta with an 8-bit accumulator and a 7-bit sample, 8 output bits per
video line. For accumulator acc (0..255) and sample v7 (0..127) the 8 steps are
    s = acc + 2*v7; bit = s >> 8; acc = s & 255
and the first step's bit is pattern bit 7 (shifted out first by CPLD2).
Entry index = (acc << 7) | v7; entry = (final acc << 8) | pattern, big-endian 16-bit, so a
word write of the entry to IO_AUDIO delivers the pattern in the low byte.
The BIOS keeps the accumulator pre-shifted (acc << 8) so the index is ((acc<<8) | sample) >> 1.
32768 entries = 64 KB, generated at build time into fw/build/ and .incbin'd by pdmtab.S.
"""
import sys

def entry(acc, v7):
    pattern = 0
    for _ in range(8):
        s = acc + 2 * v7
        pattern = (pattern << 1) | (s >> 8)
        acc = s & 255
    return (acc << 8) | pattern

def main():
    out = bytearray()
    for acc in range(256):
        for v7 in range(128):
            out += entry(acc, v7).to_bytes(2, "big")
    with open(sys.argv[1], "wb") as f:
        f.write(out)

if __name__ == "__main__":
    main()
