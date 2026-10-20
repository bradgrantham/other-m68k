# other-m68k SDK: libsys.a (syscall stubs + newlib shim), crt0, and example programs.
# See README.md. Add a program: put foo.c in examples/ and add it to EXAMPLES.

XTOOLS ?= $(HOME)/x-tools/m68k-unknown-elf/bin
CROSS   = $(XTOOLS)/m68k-unknown-elf-
CC      = $(CROSS)gcc
AR      = $(CROSS)ar
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size
GEN = ../generated
B   = build

CFLAGS  = -m68000 -Os -Wall -Wextra -ffunction-sections -fdata-sections -I$(GEN) -I.
ASFLAGS = -m68000 -I$(GEN) -I. -x assembler-with-cpp
# newlib in this toolchain expects the libgloss names (write/read/sbrk/_exit/...), which
# newlib_shim.c provides; libsys comes after libc so libc's references resolve into it.
LDFLAGS = -m68000 -nostartfiles -T user.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -L$(B)
LDLIBS  = -Wl,--start-group -lc -lsys -Wl,--end-group -lgcc

EXAMPLES = hello plasma

.PHONY: all clean check-sources

all: check-sources $(B)/libsys.a $(B)/crt0.o $(EXAMPLES:%=$(B)/%.elf) $(EXAMPLES:%=$(B)/%.srec)

check-sources:
	@test -f crt0.S -a -f newlib_shim.c || { echo "sdk: sources are not written yet (crt0.S / newlib_shim.c missing)."; \
	  echo "sdk: see fw/STATUS.md for the design and next steps."; exit 1; }

$(B):
	mkdir -p $(B)

$(B)/libsys.a: $(B)/syscall_stubs.o $(B)/newlib_shim.o
	$(AR) rcs $@ $^

$(B)/syscall_stubs.o: $(GEN)/syscall_stubs.S | $(B)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(B)/crt0.o: crt0.S | $(B)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/%.o: examples/%.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/%.elf: $(B)/%.o $(B)/crt0.o $(B)/libsys.a user.ld $(GEN)/user.ld
	$(CC) $(LDFLAGS) -o $@ $(B)/crt0.o $< $(LDLIBS)
	$(SIZE) $@

$(B)/%.srec: $(B)/%.elf
	$(OBJCOPY) -O srec $< $@

clean:
	rm -rf $(B)
