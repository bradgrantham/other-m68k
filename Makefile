# Top-level build. Sub-makes live in fw/, emu/, sdk/, hdl/.
XTOOLS ?= $(HOME)/x-tools/m68k-unknown-elf/bin
export XTOOLS

.PHONY: all gen check-gen fw emu sdk hdl run clean

all: gen fw emu sdk

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

run: fw emu
	./emu/build/m68k-emu --rom fw/build/rom.bin $(EMUFLAGS)

clean:
	-$(MAKE) -C fw clean
	-$(MAKE) -C emu clean
	-$(MAKE) -C sdk clean
	-$(MAKE) -C hdl clean
