#!/usr/bin/env python3
"""Finalise the ROM image, and build the flash window image and the two chip images.

  mkrom.py rom.bin
      Pad rom.bin to ROM_IMAGE_SIZE with 0xFF and set its last word so that the 16-bit sum
      of all big-endian words is 0x0000 (the POST sums the image and expects 0; the banner
      shows the stored word as "ROM sum"). This is the file for the monitor's `flash` command.

  mkrom.py --split flash.img rom_even.bin rom_odd.bin rom.bin [volume.img]
      flash.img is the whole 1 MB window: rom.bin followed, at FLASH_VOL_OFFSET, by the C:
      volume image (FLASH_VOL_SIZE bytes, 0xFF = unformatted when omitted), for the
      emulator's --rom (with --rom-save to keep what the machine writes to C:). The chip
      images are its byte lanes: even addresses (D15..D8) -> rom_even.bin for U4, odd
      (D7..D0) -> rom_odd.bin for U5, FLASH_CHIP_SIZE each, for the TL866.

Sizes come from ../generated/hw.h.
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


def checksum(path):
    image_size = hw_const("ROM_IMAGE_SIZE")
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) > image_size:
        sys.exit("mkrom: image is %d bytes, larger than the %d byte ROM image" % (len(rom), image_size))
    rom += b"\xff" * (image_size - len(rom))
    used = image_size - 2
    while used > 0 and rom[used - 1] == 0xFF:
        used -= 1
    total = sum(((rom[i] << 8) | rom[i + 1]) for i in range(0, image_size - 2, 2))
    ck = (-total) & 0xFFFF
    rom[image_size - 2] = ck >> 8
    rom[image_size - 1] = ck & 0xFF
    assert sum(((rom[i] << 8) | rom[i + 1]) for i in range(0, image_size, 2)) & 0xFFFF == 0
    with open(path, "wb") as f:
        f.write(rom)
    print("mkrom: %d byte image, checksum word 0x%04X, ~%d bytes used before trailing 0xFF fill"
          % (image_size, ck, used))


def split(flash_path, even_path, odd_path, rom_path, vol_path):
    image_size = hw_const("ROM_IMAGE_SIZE")
    chip_size = hw_const("FLASH_CHIP_SIZE")
    vol_off = hw_const("FLASH_VOL_OFFSET")
    vol_size = hw_const("FLASH_VOL_SIZE")
    window = 2 * chip_size
    with open(rom_path, "rb") as f:
        rom = f.read()
    if len(rom) != image_size:
        sys.exit("mkrom: %s is %d bytes, not ROM_IMAGE_SIZE (%d): run mkrom.py on it first" % (rom_path, len(rom), image_size))
    img = bytearray(b"\xff" * window)
    img[:image_size] = rom
    if vol_path:
        with open(vol_path, "rb") as f:
            vol = f.read()
        if len(vol) > vol_size:
            sys.exit("mkrom: %s is %d bytes, larger than the %d byte volume" % (vol_path, len(vol), vol_size))
        img[vol_off:vol_off + len(vol)] = vol
    with open(flash_path, "wb") as f:
        f.write(img)
    with open(even_path, "wb") as f:
        f.write(img[0::2])
    with open(odd_path, "wb") as f:
        f.write(img[1::2])
    print("mkrom: %s = %d KB BIOS image + %s at %d KB; chip images %d bytes each"
          % (flash_path, image_size // 1024, ("%d KB volume" % (len(vol) // 1024)) if vol_path else "no volume (0xFF)",
             vol_off // 1024, chip_size))


def main():
    a = sys.argv[1:]
    if len(a) == 1:
        checksum(a[0])
    elif a and a[0] == "--split" and len(a) in (5, 6):
        split(a[1], a[2], a[3], a[4], a[5] if len(a) == 6 else None)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
