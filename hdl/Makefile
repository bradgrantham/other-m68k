# hdl/ -- glue logic for the other-m68k board: two ATF1508AS CPLDs and a system testbench.
#
#   make sim     simulate (iverilog + vvp); exits nonzero if any test fails
#   make wave    same, writing build/tb_system.vcd
#   make lint    iverilog -tnull on the synthesisable sources
#   make synth   yosys generic synthesis + cell statistics for each CPLD (build/synth_*.log)
#   make fit     yosys -> EDIF -> Atmel fitter (wine) -> build/fit/*.jed; see fit.sh
#   make clean

IVERILOG ?= iverilog
VVP      ?= vvp
YOSYS    ?= yosys

GEN      := ../generated
IVFLAGS  := -g2005 -Wall -Wno-timescale -I$(GEN)

RTL      := cpld1_mmu.v cpld2_io.v
TB       := tb/tb_m68k_bus.v tb/tb_sram.v tb/tb_system.v
BUILD    := build

.PHONY: all sim wave lint synth fit clean

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
	./fit.sh cpld1_mmu cpld1_mmu.v cpld1_mmu -- $(FITFLAGS)
	./fit.sh cpld2_io  cpld2_io.v  cpld2_io  -- $(FITFLAGS)

clean:
	rm -rf $(BUILD)
