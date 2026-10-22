# Top-level build. Sub-makes live in fw/, emu/, sdk/, hdl/.
XTOOLS ?= $(HOME)/x-tools/m68k-unknown-elf/bin
export XTOOLS

.SECONDEXPANSION:
.PHONY: all gen check-gen fw emu sdk hdl flash run clean

all: gen fw emu sdk flash

gen:
	python3 tools/gen.py

check-gen:
	python3 tools/gen.py --check

fw: gen
	$(MAKE) -C fw

sdk: gen fw
	$(MAKE) -C sdk

emu: gen
	$(MAKE) -C emu

hdl:
	$(MAKE) -C hdl

# The flash window image: the BIOS image plus the C: volume (flashfs/ and the SDK's example
# programs), and the two chip images for the TL866. `make run` boots it and keeps what the
# machine writes to C: in fw/build/flash.img across runs (--rom-save).
FLASHFS_PROGRAMS = hello plasma ls cat fstest bmpview tune abench sdplay term joytest
FLASHFS_FILES = flashfs/AUTOEXEC.CMD flashfs/README.TXT $(foreach p,$(FLASHFS_PROGRAMS),fw/build/cfiles/$(shell echo $(p) | tr a-z A-Z).ELF)

flash: fw sdk fw/build/flash.img

# programs go in stripped (the loader reads only the program headers; symbols would double the size)
fw/build/cfiles/%.ELF: sdk/build/$$(shell echo % | tr A-Z a-z).elf
	mkdir -p fw/build/cfiles
	$(XTOOLS)/m68k-unknown-elf-strip -o $@ $<

fw/build/c.img: tools/mkflashvol.sh $(FLASHFS_FILES) generated/hw.h
	sh tools/mkflashvol.sh $@ $(FLASHFS_FILES)

fw/build/flash.img fw/build/rom_even.bin fw/build/rom_odd.bin: fw/build/rom.bin fw/build/c.img fw/mkrom.py
	python3 fw/mkrom.py --split fw/build/flash.img fw/build/rom_even.bin fw/build/rom_odd.bin fw/build/rom.bin fw/build/c.img

run: flash emu
	./emu/build/m68k-emu --rom fw/build/flash.img --rom-save fw/build/flash.img $(EMUFLAGS)

clean:
	-$(MAKE) -C fw clean
	-$(MAKE) -C emu clean
	-$(MAKE) -C sdk clean
	-$(MAKE) -C hdl clean
