# other-m68k firmware (BIOS/monitor ROM) build.
# Outputs: build/rom.elf, build/rom.bin (256 KB = ROM_IMAGE_SIZE, 0xFF padded, checksum in
#          the last word: the file for the monitor's `flash` command), build/rom.lst.
#          The chip images for the TL866 (rom_even.bin / rom_odd.bin) also carry the C:
#          volume, so the top-level `make flash` builds them (mkrom.py --split).
# `make` builds everything; `make test` runs the host unit tests of the pure-C modules.

XTOOLS ?= $(HOME)/x-tools/m68k-unknown-elf/bin
CROSS   = $(XTOOLS)/m68k-unknown-elf-
CC      = $(CROSS)gcc
AS      = $(CROSS)gcc
LD      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
SIZE    = $(CROSS)size
HOSTCC ?= gcc
PYTHON ?= python3

GEN  = ../generated
B    = build
FW_VERSION ?= $(shell git describe --always --dirty 2>/dev/null || echo dev)

CFLAGS  = -m68000 --param=min-pagesize=0 -Os -Wall -Wextra -ffreestanding -nostdlib -fno-delete-null-pointer-checks \
          -fno-common -ffunction-sections -fdata-sections -I$(GEN) -I. -Ifatfs -DFW_VERSION=\"$(FW_VERSION)\"
ASFLAGS = -m68000 -I$(GEN) -I. -x assembler-with-cpp
LDFLAGS = -m68000 -nostdlib -ffreestanding -T rom.ld -Wl,--gc-sections -Wl,-Map=$(B)/rom.map

# Planned source list (see STATUS.md); only files that exist are built.
ASRCS = crt0.S post.S duart.S memops.S sd4.S flashram.S irq_hsync.S
CSRCS = main.c console.c serial.c input.c ps2.c irq.c exception.c monitor.c srec.c queue.c \
        hexdump.c printf.c syscall.c string.c sd.c diskio.c fs.c elf.c exec.c flash.c audio.c \
        clock.c flashdisk.c args.c lineedit.c joy.c
FATFS = fatfs/ff.c fatfs/ffunicode.c
OBJS  = $(patsubst %.S,$(B)/%.o,$(ASRCS)) $(patsubst %.c,$(B)/%.o,$(CSRCS)) $(B)/ff.o $(B)/ffunicode.o

.PHONY: all clean test check-sources

all: check-sources $(B)/rom.bin $(B)/rom.lst

check-sources:
	@test -f crt0.S || { echo "fw: firmware sources are not written yet (crt0.S missing)."; \
	  echo "fw: see fw/STATUS.md for the design, decisions and next steps."; exit 1; }

$(B):
	mkdir -p $(B)

$(B)/%.o: %.S | $(B)
	$(AS) $(ASFLAGS) -MMD -MP -c -o $@ $<

$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# header dependencies (generated/hw.h and friends change with the spec)
-include $(wildcard $(B)/*.d)

# FatFs R0.15 (fatfs/, configured by fatfs/ffconf.h); vendor code, warnings not our business.
$(B)/ff.o: fatfs/ff.c fatfs/ffconf.h | $(B)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(B)/ffunicode.o: fatfs/ffunicode.c fatfs/ffconf.h | $(B)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(B)/rom.elf: $(OBJS) rom.ld $(GEN)/memory.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) -lgcc
	$(SIZE) $@

# ROM_IMAGE_SIZE image, gaps 0xFF (the .romtail section puts the last word at the end of
# the image, so objcopy emits the full size); mkrom.py patches the checksum word so that
# the 16-bit sum of all big-endian words is 0 (size read from ../generated/hw.h).
$(B)/rom.bin: $(B)/rom.elf mkrom.py
	$(OBJCOPY) -O binary --gap-fill=0xFF $< $@
	$(PYTHON) mkrom.py $@

$(B)/rom.lst: $(B)/rom.elf
	$(OBJDUMP) -d -S $< > $@

# Host-side unit tests: pure-C modules (srec, ps2, printf, console, hexdump, queue), the SD
# driver against the emulator's card model, the ELF parser (synthetic image and
# ../sdk/build/hello.elf when present) and FatFs + the fd table against a FAT32 image made
# with mkfs.vfat/mcopy (skipped with a message when mtools/dosfstools are missing).
TEST_SRCS = test/test_main.c test/test_sd.c test/test_elf.c test/test_fs.c test/test_audio.c \
            srec.c ps2.c printf.c console.c hexdump.c queue.c sd.c fs.c elf.c audio.c clock.c flashdisk.c args.c joy.c $(FATFS)
TEST_IMG  = $(B)/test/fat.img
TEST_CIMG = $(B)/test/c.img

# The SD tests drive the emulator's card model (../emu/sdcard.c); its lifecycle functions
# are renamed so they do not collide with the BIOS's sd_init.
test: $(TEST_IMG) $(TEST_CIMG)
	mkdir -p $(B)/test
	$(HOSTCC) -std=gnu11 -Wall -Wextra -O1 -g -I$(GEN) -I../emu -Dsd_init=emu_sd_init \
	    -Dsd_shutdown=emu_sd_shutdown -Dsd_reset=emu_sd_reset -c -o $(B)/test/emu_sdcard.o ../emu/sdcard.c
	$(HOSTCC) -std=gnu11 -Wall -Wextra -O1 -g -DHOST_TEST -I$(GEN) -I. -Ifatfs -Itest \
	    -Wno-unused-parameter -o $(B)/test/fwtest $(TEST_SRCS) $(B)/test/emu_sdcard.o
	$(B)/test/fwtest $(TEST_IMG) ../sdk/build/hello.elf $(TEST_CIMG)

# 64 MB sparse FAT32 image with a few files (FAT32 needs >= 65525 clusters, so 32 MB is
# too small for mkfs.fat with 512-byte clusters).
$(TEST_IMG): test/mkimage.sh
	mkdir -p $(B)/test
	sh test/mkimage.sh $@ 64 test/data/README.TXT test/data/HELLO.TXT || { rm -f $@; touch $@; }

# the C: volume image (FAT12, laid out as `format C:` does), for the flash-drive tests
$(TEST_CIMG): ../tools/mkflashvol.sh $(GEN)/hw.h
	mkdir -p $(B)/test
	sh ../tools/mkflashvol.sh $@ test/data/README.TXT || { rm -f $@; touch $@; }

clean:
	rm -rf $(B)
