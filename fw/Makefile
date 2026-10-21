# other-m68k firmware (BIOS/monitor ROM) build.
# Outputs: build/rom.elf, build/rom.bin (128 KB, 0xFF padded, checksum in last word: the
#          file for the monitor's `flash` command), build/rom.lst, build/rom_even.bin
#          (U4, D15..D8) and build/rom_odd.bin (U5, D7..D0): 512 KB each, 0xFF padded, for
#          programming the SST39SF040 chips in the TL866.
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

CFLAGS  = -m68000 -Os -Wall -Wextra -ffreestanding -nostdlib -fno-delete-null-pointer-checks \
          -fno-common -ffunction-sections -fdata-sections -I$(GEN) -I. -Ifatfs -DFW_VERSION=\"$(FW_VERSION)\"
ASFLAGS = -m68000 -I$(GEN) -I. -x assembler-with-cpp
LDFLAGS = -m68000 -nostdlib -ffreestanding -T rom.ld -Wl,--gc-sections -Wl,-Map=$(B)/rom.map

# Planned source list (see STATUS.md); only files that exist are built.
ASRCS = crt0.S post.S duart.S memops.S sdspi.S flashram.S
CSRCS = main.c console.c serial.c input.c ps2.c irq.c exception.c monitor.c srec.c queue.c \
        hexdump.c printf.c syscall.c string.c sd.c diskio.c fs.c elf.c exec.c flash.c
FATFS = fatfs/ff.c
OBJS  = $(patsubst %.S,$(B)/%.o,$(ASRCS)) $(patsubst %.c,$(B)/%.o,$(CSRCS)) $(B)/ff.o

.PHONY: all clean test check-sources

all: check-sources $(B)/rom.bin $(B)/rom.lst $(B)/rom_even.bin $(B)/rom_odd.bin

check-sources:
	@test -f crt0.S || { echo "fw: firmware sources are not written yet (crt0.S missing)."; \
	  echo "fw: see fw/STATUS.md for the design, decisions and next steps."; exit 1; }

$(B):
	mkdir -p $(B)

$(B)/%.o: %.S | $(B)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<

# FatFs R0.15 (fatfs/, configured by fatfs/ffconf.h); vendor code, warnings not our business.
$(B)/ff.o: $(FATFS) fatfs/ffconf.h | $(B)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(B)/rom.elf: $(OBJS) rom.ld $(GEN)/memory.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) -lgcc
	$(SIZE) $@

# 128 KB image (ROM_IMAGE_SIZE), gaps 0xFF; mkrom.py patches the checksum word (last word)
# so that the 16-bit sum of all 65536 big-endian words is 0, and writes the even/odd split
# padded to the flash chip size (sizes read from ../generated/hw.h).
$(B)/rom.bin $(B)/rom_even.bin $(B)/rom_odd.bin: $(B)/rom.elf mkrom.py
	$(OBJCOPY) -O binary --gap-fill=0xFF --pad-to=0xE20000 $< $(B)/rom.bin
	$(PYTHON) mkrom.py $(B)/rom.bin $(B)/rom_even.bin $(B)/rom_odd.bin

$(B)/rom.lst: $(B)/rom.elf
	$(OBJDUMP) -d -S $< > $@

# Host-side unit tests: pure-C modules (srec, ps2, printf, console, hexdump, queue), SD
# command framing against a fake card, the ELF parser (synthetic image and
# ../sdk/build/hello.elf when present) and FatFs + the fd table against a FAT32 image made
# with mkfs.vfat/mcopy (skipped with a message when mtools/dosfstools are missing).
TEST_SRCS = test/test_main.c test/test_sd.c test/test_elf.c test/test_fs.c \
            srec.c ps2.c printf.c console.c hexdump.c queue.c sd.c fs.c elf.c $(FATFS)
TEST_IMG  = $(B)/test/fat.img

test: $(TEST_IMG)
	mkdir -p $(B)/test
	$(HOSTCC) -std=gnu11 -Wall -Wextra -O1 -g -DHOST_TEST -I$(GEN) -I. -Ifatfs -Itest \
	    -Wno-unused-parameter -o $(B)/test/fwtest $(TEST_SRCS)
	$(B)/test/fwtest $(TEST_IMG) ../sdk/build/hello.elf

# 64 MB sparse FAT32 image with a few files (FAT32 needs >= 65525 clusters, so 32 MB is
# too small for mkfs.fat with 512-byte clusters).
$(TEST_IMG): test/mkimage.sh
	mkdir -p $(B)/test
	sh test/mkimage.sh $@ 64 test/data/README.TXT test/data/HELLO.TXT || { rm -f $@; touch $@; }

clean:
	rm -rf $(B)
