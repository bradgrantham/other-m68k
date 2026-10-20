# other-m68k firmware (BIOS/monitor ROM) build.
# Outputs: build/rom.elf, build/rom.bin (128 KB, 0xFF padded, checksum in last word),
#          build/rom.lst, build/rom_even.bin (D15..D8), build/rom_odd.bin (D7..D0).
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
          -fno-common -ffunction-sections -fdata-sections -I$(GEN) -I. -DFW_VERSION=\"$(FW_VERSION)\"
ASFLAGS = -m68000 -I$(GEN) -I. -x assembler-with-cpp
LDFLAGS = -m68000 -nostdlib -ffreestanding -T rom.ld -Wl,--gc-sections -Wl,-Map=$(B)/rom.map

# Planned source list (see STATUS.md); only files that exist are built.
ASRCS = crt0.S post.S duart.S memops.S
CSRCS = main.c console.c serial.c input.c ps2.c irq.c exception.c monitor.c srec.c queue.c \
        hexdump.c printf.c syscall.c string.c
OBJS  = $(patsubst %.S,$(B)/%.o,$(ASRCS)) $(patsubst %.c,$(B)/%.o,$(CSRCS))

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

$(B)/rom.elf: $(OBJS) rom.ld $(GEN)/memory.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) -lgcc
	$(SIZE) $@

# 128 KB image, gaps 0xFF; mkrom.py patches the checksum word (last word) so that
# the 16-bit sum of all 65536 big-endian words is 0, and writes the even/odd split.
$(B)/rom.bin $(B)/rom_even.bin $(B)/rom_odd.bin: $(B)/rom.elf mkrom.py
	$(OBJCOPY) -O binary --gap-fill=0xFF --pad-to=0xE20000 $< $(B)/rom.bin
	$(PYTHON) mkrom.py $(B)/rom.bin $(B)/rom_even.bin $(B)/rom_odd.bin

$(B)/rom.lst: $(B)/rom.elf
	$(OBJDUMP) -d -S $< > $@

# Host-side unit tests of the pure-C modules (srec, ps2, printf, console, hexdump, queue).
test:
	@test -f test/test_main.c || { echo "fw: no host tests written yet (see STATUS.md)"; exit 1; }
	mkdir -p $(B)/test
	$(HOSTCC) -std=gnu11 -Wall -Wextra -O1 -g -DHOST_TEST -I$(GEN) -I. -o $(B)/test/fwtest \
	    test/test_main.c srec.c ps2.c printf.c console.c hexdump.c queue.c
	$(B)/test/fwtest

clean:
	rm -rf $(B)
