# other-m68k emulator.  Builds build/m68k-emu (SDL2 + Musashi 68000 core).
# Musashi's opcode handlers are generated at build time by m68kmake (host cc).

CC      ?= cc
HOSTCC  ?= cc
BUILD    = build
MUSASHI  = musashi
GEN      = ../generated

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

CONF     = $(abspath m68kconf.h)
CFLAGS_COMMON = -std=gnu11 -O2 -g -I$(GEN) -I$(MUSASHI) -I$(BUILD) -I. \
                -DMUSASHI_CNF='"$(CONF)"'
# Own sources: warnings are errors-in-spirit (must be clean).
CFLAGS   = $(CFLAGS_COMMON) -Wall -Wextra $(SDL_CFLAGS)
# Vendored core: compile quietly.
CFLAGS_MUSASHI = $(CFLAGS_COMMON) -w

# Emulator sources (emu/*.c).  See STATUS.md for what exists.
EMU_SRCS := $(wildcard *.c)
EMU_OBJS  = $(EMU_SRCS:%.c=$(BUILD)/%.o)

MUS_OBJS  = $(BUILD)/m68kcpu.o $(BUILD)/m68kops.o $(BUILD)/m68kdasm.o $(BUILD)/softfloat.o
LIBS      = $(SDL_LIBS) -lm

.PHONY: all core clean test

all: $(BUILD)/m68k-emu

core: $(MUS_OBJS)

ifeq ($(EMU_SRCS),)
$(BUILD)/m68k-emu: core
	@echo "error: emulator sources (emu/main.c etc.) are not written yet; see emu/STATUS.md" >&2
	@exit 1
else
$(BUILD)/m68k-emu: $(EMU_OBJS) $(MUS_OBJS)
	$(CC) -o $@ $^ $(LIBS)
endif

$(BUILD):
	mkdir -p $(BUILD)

# --- Musashi generator and core ---
$(BUILD)/m68kmake: $(MUSASHI)/m68kmake.c | $(BUILD)
	$(HOSTCC) -O1 -w -o $@ $<

$(BUILD)/m68kops.c $(BUILD)/m68kops.h: $(BUILD)/m68kmake $(MUSASHI)/m68k_in.c
	$(BUILD)/m68kmake $(BUILD) $(MUSASHI)/m68k_in.c > /dev/null

$(BUILD)/m68kcpu.o: $(MUSASHI)/m68kcpu.c $(BUILD)/m68kops.h $(MUSASHI)/m68kcpu.h $(MUSASHI)/m68kfpu.c $(MUSASHI)/m68kmmu.h m68kconf.h | $(BUILD)
	$(CC) $(CFLAGS_MUSASHI) -c -o $@ $<

$(BUILD)/m68kops.o: $(BUILD)/m68kops.c $(BUILD)/m68kops.h $(MUSASHI)/m68kcpu.h m68kconf.h
	$(CC) $(CFLAGS_MUSASHI) -c -o $@ $<

$(BUILD)/m68kdasm.o: $(MUSASHI)/m68kdasm.c $(MUSASHI)/m68k.h m68kconf.h | $(BUILD)
	$(CC) $(CFLAGS_MUSASHI) -c -o $@ $<

$(BUILD)/softfloat.o: $(MUSASHI)/softfloat/softfloat.c m68kconf.h | $(BUILD)
	$(CC) $(CFLAGS_MUSASHI) -c -o $@ $<

# --- own sources ---
$(BUILD)/%.o: %.c $(wildcard *.h) m68kconf.h $(BUILD)/m68kops.h | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# header dependencies (generated/hw.h and emu_map.h change with the spec)
-include $(wildcard $(BUILD)/*.d)

test: all
	$(MAKE) -C test

clean:
	rm -rf $(BUILD)
	-$(MAKE) -C test clean 2>/dev/null || true
