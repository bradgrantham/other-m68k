# hdl/ -- glue logic for the other-m68k board: two ATF1508AS CPLDs and a system testbench.
#
#   make sim     simulate (iverilog + vvp); exits nonzero if any test fails
#   make wave    same, writing build/tb_system.vcd
#   make lint    iverilog -tnull on the synthesisable sources
#   make synth   yosys generic synthesis + cell statistics for each CPLD (build/synth_*.log)
#   make clean

IVERILOG ?= iverilog
VVP      ?= vvp
YOSYS    ?= yosys

GEN      := ../generated
IVFLAGS  := -g2005 -Wall -Wno-timescale -I$(GEN)

RTL      := cpld1_mmu.v cpld2_io.v
TB       := tb/tb_m68k_bus.v tb/tb_sram.v tb/tb_system.v
BUILD    := build

.PHONY: all sim sim-notx wave lint synth clean

all: lint sim sim-notx synth

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tb_system.vvp: $(RTL) $(TB) $(GEN)/board_params.vh | $(BUILD)
	$(IVERILOG) $(IVFLAGS) -o $@ $(RTL) $(TB)

sim: $(BUILD)/tb_system.vvp
	$(VVP) -N $< | tee $(BUILD)/sim.log
	@grep -q '^RESULT: PASS' $(BUILD)/sim.log

# same testbench with the hardware serial transmitter removed (HW_TX=0)
$(BUILD)/tb_system_notx.vvp: $(RTL) $(TB) $(GEN)/board_params.vh | $(BUILD)
	$(IVERILOG) $(IVFLAGS) -P tb_system.HW_TX=0 -o $@ $(RTL) $(TB)

sim-notx: $(BUILD)/tb_system_notx.vvp
	$(VVP) -N $< | tee $(BUILD)/sim_notx.log
	@grep -q '^RESULT: PASS' $(BUILD)/sim_notx.log

wave: $(BUILD)/tb_system.vvp
	$(VVP) -N $< +vcd | tee $(BUILD)/sim.log
	@grep -q '^RESULT: PASS' $(BUILD)/sim.log

lint: | $(BUILD)
	$(IVERILOG) $(IVFLAGS) -tnull $(RTL)

synth: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth_cpld1_mmu.log -p 'read_verilog -I$(GEN) cpld1_mmu.v; synth -top cpld1_mmu' >/dev/null
	$(YOSYS) -q -l $(BUILD)/synth_cpld2_io.log  -p 'read_verilog -I$(GEN) cpld2_io.v;  synth -top cpld2_io' >/dev/null
	$(YOSYS) -q -l $(BUILD)/synth_cpld2_io_notx.log -p 'read_verilog -I$(GEN) cpld2_io.v; chparam -set HW_TX 0 cpld2_io; synth -top cpld2_io' >/dev/null
	@for f in cpld1_mmu cpld2_io cpld2_io_notx; do \
	  echo "== $$f"; sed -n '/^=== /,$$p' $(BUILD)/synth_$$f.log | grep -E 'Number of cells|\$$_' ; done

clean:
	rm -rf $(BUILD)
