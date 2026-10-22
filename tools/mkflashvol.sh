#!/bin/sh
# mkflashvol.sh IMAGE file...  -- the C: volume image: a 768 KB FAT12 volume with the given
# files in its root, with the geometry of the BIOS's `format C:` where it matters (spec
# flash.volume_desc: 512-byte sectors, 4 KB clusters, two FATs, 224 root entries, data area
# at sector 32 so clusters sit inside 8 KB erase sectors; the reserved-sector and FAT sizes
# differ from FatFs's f_mkfs, which is harmless). Needs mkfs.vfat (dosfstools) and mcopy (mtools).
# Files are stored under their upper-cased names (8.3 where possible, so no LFN entries are
# spent on them); a directory argument is copied recursively.
set -e
img="$1"; shift
for t in mkfs.vfat mcopy; do
    command -v "$t" >/dev/null 2>&1 || PATH="$PATH:/sbin:/usr/sbin" command -v "$t" >/dev/null 2>&1 || {
        echo "mkflashvol: $t not found (install dosfstools and mtools)" >&2; exit 1; }
done
PATH="$PATH:/sbin:/usr/sbin"
size_kb=$(python3 -c "import re;print(int(re.search(r'#define FLASH_VOL_SIZE (0x[0-9A-Fa-f]+)',open('$(dirname "$0")/../generated/hw.h').read()).group(1),16)//1024)")
rm -f "$img"
# erased flash reads 0xFF: pre-fill so the unused data area stays erased on the chips
head -c "$((size_kb * 1024))" /dev/zero | tr '\0' '\377' > "$img"
mkfs.vfat -F 12 -S 512 -s 8 -f 2 -r 224 -R 16 -n M68KFLASH "$img" >/dev/null
for f in "$@"; do
    name=$(basename "$f" | tr '[:lower:]' '[:upper:]')
    MTOOLS_SKIP_CHECK=1 mcopy -s -i "$img" "$f" "::/$name"
done
echo "mkflashvol: $img (${size_kb} KB FAT12): $*"
