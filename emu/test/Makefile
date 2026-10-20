# Emulator self-test: builds a small 68000 ROM, runs it headless and checks
# the results it leaves in RAM plus the decoded serial output.
XTOOLS ?= $(HOME)/x-tools/m68k-unknown-elf/bin
CROSS   = $(XTOOLS)/m68k-unknown-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
GEN     = ../../generated
B       = build
EMU     = ../build/m68k-emu

.PHONY: all check clean rom

all: check

rom: $(B)/test.bin $(B)/test.lst

$(B):
	mkdir -p $(B)

$(B)/test.elf: test.S test.ld $(GEN)/hw.inc | $(B)
	$(CC) -m68000 -nostdlib -nostartfiles -x assembler-with-cpp -I$(GEN) -T test.ld -Wl,--no-warn-rwx-segments -o $@ test.S

$(B)/test.bin: $(B)/test.elf
	$(OBJCOPY) -O binary $< $@

$(B)/test.lst: $(B)/test.elf
	$(OBJDUMP) -d $< > $@

# 1 MB card image: block 0 carries a signature the ROM reads back.
$(B)/sd.img: | $(B)
	rm -f $@
	printf 'M68K-SD-TEST-BLOCK0\n' > $@
	truncate -s 1M $@

check: $(B)/test.bin $(B)/test.lst $(B)/sd.img
	@test -x $(EMU) || { echo "test: build the emulator first (make -C emu)"; exit 1; }
	./check.sh $(EMU) $(B)

clean:
	rm -rf $(B)
