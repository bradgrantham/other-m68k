#!/usr/bin/env python3
"""Finalise the ROM image: fix the checksum word and split into even/odd EPROM images.

Usage: mkrom.py rom.bin rom_even.bin rom_odd.bin

Checksum: the ROM is 65536 big-endian 16-bit words. The last word (offset 0x1FFFE) is
set so that the 16-bit sum of all words (including the checksum word) is 0x0000.
The POST verifies this by summing every word and expecting 0. The stored word itself
is what the banner shows as "ROM checksum".

Split: even byte addresses (D15..D8, the upper byte of each word) -> rom_even.bin,
odd byte addresses (D7..D0) -> rom_odd.bin. Each is 64 KB for a 27C512.
"""
import sys

ROM_SIZE = 0x20000

def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    path, even_path, odd_path = sys.argv[1:4]
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) > ROM_SIZE:
        sys.exit("mkrom: image is %d bytes, larger than the %d byte ROM" % (len(rom), ROM_SIZE))
    rom += b"\xff" * (ROM_SIZE - len(rom))
    used = ROM_SIZE - 2
    while used > 0 and rom[used - 1] == 0xFF:
        used -= 1
    total = 0
    for i in range(0, ROM_SIZE - 2, 2):
        total += (rom[i] << 8) | rom[i + 1]
    ck = (-total) & 0xFFFF
    rom[ROM_SIZE - 2] = ck >> 8
    rom[ROM_SIZE - 1] = ck & 0xFF
    # verify
    s = sum(((rom[i] << 8) | rom[i + 1]) for i in range(0, ROM_SIZE, 2)) & 0xFFFF
    assert s == 0, "checksum verify failed"
    with open(path, "wb") as f:
        f.write(rom)
    with open(even_path, "wb") as f:
        f.write(rom[0::2])
    with open(odd_path, "wb") as f:
        f.write(rom[1::2])
    print("mkrom: %d bytes, checksum word 0x%04X, ~%d bytes used before trailing 0xFF fill"
          % (ROM_SIZE, ck, used))

if __name__ == "__main__":
    main()
