#!/bin/sh
# mkimage.sh IMAGE SIZE_MB file...  -- sparse FAT32 image with the given files in the root.
# Needs mkfs.vfat (dosfstools) and mcopy (mtools). Exit 1 (with a message) if missing.
set -e
img="$1"; mb="$2"; shift 2
for t in mkfs.vfat mcopy; do
    command -v "$t" >/dev/null 2>&1 || PATH="$PATH:/sbin:/usr/sbin" command -v "$t" >/dev/null 2>&1 || {
        echo "mkimage: $t not found (install dosfstools and mtools); skipping FAT image" >&2; exit 1; }
done
PATH="$PATH:/sbin:/usr/sbin"
rm -f "$img"
truncate -s "${mb}M" "$img"
mkfs.vfat -F 32 -n M68K "$img" >/dev/null
for f in "$@"; do
    MTOOLS_SKIP_CHECK=1 mcopy -i "$img" "$f" ::/
done
echo "mkimage: $img (${mb} MB FAT32): $*"
