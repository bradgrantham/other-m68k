# hdl/ -- glue logic for the other-m68k board: two ATF1508AS CPLDs and a system testbench.
#
#   make sim     simulate (iverilog + vvp); exits nonzero if any test fails
#   make wave    same, writing build/tb_system.vcd
#   make lint    iverilog -tnull on the synthesisable sources
#   make synth   yosys generic synthesis + cell statistics for each CPLD (build/synth_*.log)
#   make fit     yosys -> EDIF -> Atmel fitter (wine) -> build/fit/*.jed; see fit.sh
#   make svf     build/fit/*.jed -> build/fit/*.svf with prjbureau's fuseconv (needs PRJBUREAU)
#   make jtag-scan                 list the TAPs OpenOCD finds (expect two, 0x0150803f)
#   make flash-cpld1               play cpld1_mmu.svf into U6 (tap1) (FT232H/UM232H, OpenOCD)
#   make flash-cpld2               play cpld2_io.svf into U7 (tap0)
#   make clean

IVERILOG ?= iverilog
VVP      ?= vvp
YOSYS    ?= yosys

GEN      := ../generated
IVFLAGS  := -g2005 -Wall -Wno-timescale -I$(GEN)

RTL      := cpld1_mmu.v cpld2_io.v
TB       := tb/tb_m68k_bus.v tb/tb_sram.v tb/tb_system.v
BUILD    := build

.PHONY: all sim wave lint synth fit svf jtag-scan flash-cpld1 flash-cpld2 pins-check clean

all: lint sim synth

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tb_system.vvp: $(RTL) $(TB) $(GEN)/board_params.vh | $(BUILD)
	$(IVERILOG) $(IVFLAGS) -o $@ $(RTL) $(TB)

sim: $(BUILD)/tb_system.vvp
	$(VVP) -N $< | tee $(BUILD)/sim.log
	@grep -q '^RESULT: PASS' $(BUILD)/sim.log

wave: $(BUILD)/tb_system.vvp
	$(VVP) -N $< +vcd | tee $(BUILD)/sim.log
	@grep -q '^RESULT: PASS' $(BUILD)/sim.log

lint: | $(BUILD)
	$(IVERILOG) $(IVFLAGS) -tnull $(RTL)

synth: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth_cpld1_mmu.log -p 'read_verilog -I$(GEN) cpld1_mmu.v; synth -top cpld1_mmu' >/dev/null
	$(YOSYS) -q -l $(BUILD)/synth_cpld2_io.log  -p 'read_verilog -I$(GEN) cpld2_io.v;  synth -top cpld2_io' >/dev/null
	@for f in cpld1_mmu cpld2_io; do \
	  echo "== $$f"; sed -n '/^=== /,$$p' $(BUILD)/synth_$$f.log | grep -E 'Number of cells|\$$_' ; done

# Atmel fitter (wine) via fit.sh: build/fit/<name>.{fit,jed}.  Pin blocks are the
# "//PIN:" lines in each source.  Serial: the fitter must not run twice at once.
FITFLAGS ?= -preassign keep -strategy JTAG = on -strategy power_reset on -strategy TDI_pullup = on -strategy TMS_pullup = on
fit:
	./fit.sh cpld1_mmu cpld1_mmu.v cpld1_mmu -- $(FITFLAGS) -strategy xor_synthesis = on
	./fit.sh cpld2_io  cpld2_io.v  cpld2_io  -- $(FITFLAGS)

# JED -> SVF: whitequark/prjbureau's util.fuseconv, run from its checkout (it needs the
# python module bitarray; PRJBUREAU_PYDEPS is an optional directory holding it).
PRJBUREAU        ?= $(HOME)/.local/src/prjbureau
PRJBUREAU_PYDEPS ?= $(HOME)/.local/src/prjbureau-pydeps
FIT := $(BUILD)/fit
svf: $(FIT)/cpld1_mmu.svf $(FIT)/cpld2_io.svf
$(FIT)/%.svf: $(FIT)/%.jed
	cd $(PRJBUREAU) && PYTHONPATH=$(PRJBUREAU_PYDEPS) python3 -m util.fuseconv -d ATF1508AS $(abspath $<) $(abspath $@)

# Programming, the sequence Griffin's cpld/Makefile uses.  The chain is header TDI -> U6
# (CPLD1) -> U7 (CPLD2) -> header TDO: two ATF1508AS TAPs, IR length 10.  The SVF files
# are per-chip; OpenOCD's "svf -tap" pads the other TAP with BYPASS.  OpenOCD's first
# TAP is the one nearest TDO, so tap0 is U7 (CPLD2) and tap1 is U6 (CPLD1): confirmed on
# the first rev C3 board 2026-11-20 (cpld1_mmu.svf into tap1 gave 12 MHz CPUCLK).  TAP=
# on the command line overrides.
OPENOCD ?= sudo openocd
OPENOCD_JTAG = $(OPENOCD) -f interface/ftdi/um232h.cfg \
    -c "adapter speed 500" \
    -c "transport select jtag" \
    -c "jtag newtap tap0 tap -irlen 10 -expected-id 0x0150803f" \
    -c "jtag newtap tap1 tap -irlen 10 -expected-id 0x0150803f" \
    -c "adapter speed 2000" \
    -c init

jtag-scan:
	$(OPENOCD_JTAG) -c "scan_chain" -c shutdown

# No dependency on the .svf rule: the files are usually copied here from the build machine.
flash-cpld1 flash-cpld2: flash-%:
	@case "$(or $(TAP),$(TAP_$*))" in tap0|tap1) ;; *) echo "TAP must be tap0 or tap1"; exit 1;; esac
	@test -s $(FIT)/$(SVF_$*).svf || { echo "$(FIT)/$(SVF_$*).svf missing: make svf, or copy it here"; exit 1; }
	$(OPENOCD_JTAG) -c "svf $(FIT)/$(SVF_$*).svf -tap $(or $(TAP),$(TAP_$*)).tap" -c shutdown
SVF_cpld1 := cpld1_mmu
SVF_cpld2 := cpld2_io
TAP_cpld1 := tap1
TAP_cpld2 := tap0

pins-check:
	python3 pins.py check cpld1_mmu cpld1_mmu.v
	python3 pins.py check cpld2_io  cpld2_io.v

clean:
	rm -rf $(BUILD)
