# Editor tests: the core on the host (test_core.c, under the address sanitizer), then the
# whole editor in the emulator (needs fw/build/flash.img with EDIT.ELF on C: and
# emu/build/m68k-emu from the top-level build).
#   make check        everything
#   make host         host part only (no emulator)
B = build
.PHONY: check host clean
check:
	sh run.sh
host:
	sh run.sh host
clean:
	rm -rf $(B)
