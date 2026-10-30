# SSTV encoder tests: host encoder + decoder checks, then the emulator gate (needs
# fw/build/flash.img and emu/build/m68k-emu from the top-level build).
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
