#!/usr/bin/env python3
"""Finalise the ROM image: fix the checksum word and split into the two flash chip images.

Usage: mkrom.py rom.bin rom_even.bin rom_odd.bin

Sizes come from ../generated/hw.h: ROM_IMAGE_SIZE (128 KB, the BIOS image = what the
monitor's `flash` command programs) and FLASH_CHIP_SIZE (512 KB, one SST39SF040).

Checksum: the image is ROM_IMAGE_SIZE/2 big-endian 16-bit words. The last word is set so
that the 16-bit sum of all words (including the checksum word) is 0x0000. The POST
verifies this by summing every word of the image and expecting 0. The stored word itself
is what the banner shows as "ROM sum".

Split: even byte addresses (D15..D8, the upper byte of each word) -> rom_even.bin for U4,
odd byte addresses (D7..D0) -> rom_odd.bin for U5. Each is padded with 0xFF to the chip
size so that programming it in the TL866 fills the whole chip (the unused 448 KB of each
chip erased).
"""
import os
import re
import sys

HW_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "generated", "hw.h")


def hw_const(name):
    with open(HW_H) as f:
        m = re.search(r"#define %s (0x[0-9A-Fa-f]+)" % name, f.read())
    if not m:
        sys.exit("mkrom: %s not found in %s" % (name, HW_H))
    return int(m.group(1), 16)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    path, even_path, odd_path = sys.argv[1:4]
    image_size = hw_const("ROM_IMAGE_SIZE")
    chip_size = hw_const("FLASH_CHIP_SIZE")
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) > image_size:
        sys.exit("mkrom: image is %d bytes, larger than the %d byte ROM image" % (len(rom), image_size))
    rom += b"\xff" * (image_size - len(rom))
    used = image_size - 2
    while used > 0 and rom[used - 1] == 0xFF:
        used -= 1
    total = 0
    for i in range(0, image_size - 2, 2):
        total += (rom[i] << 8) | rom[i + 1]
    ck = (-total) & 0xFFFF
    rom[image_size - 2] = ck >> 8
    rom[image_size - 1] = ck & 0xFF
    # verify
    s = sum(((rom[i] << 8) | rom[i + 1]) for i in range(0, image_size, 2)) & 0xFFFF
    assert s == 0, "checksum verify failed"
    with open(path, "wb") as f:
        f.write(rom)
    pad = b"\xff" * (chip_size - image_size // 2)
    with open(even_path, "wb") as f:
        f.write(rom[0::2] + pad)
    with open(odd_path, "wb") as f:
        f.write(rom[1::2] + pad)
    print("mkrom: %d byte image, checksum word 0x%04X, ~%d bytes used before trailing 0xFF fill; "
          "chip images %d bytes each" % (image_size, ck, used, chip_size))


if __name__ == "__main__":
    main()
