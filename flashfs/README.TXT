This is drive C:, the 768 KB FAT12 volume in the other-m68k's flash ROM chips.
It holds the common programs (NAME.ELF) so they run from any drive, and
AUTOEXEC.CMD, which the monitor runs at boot. The SD card is drive D:.
Programs here were built from sdk/examples; replace them with `copy D:X.ELF C:`.
