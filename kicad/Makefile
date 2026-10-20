# kicad/Makefile - generate and verify the schematic/PCB with kicad-cli.
#
#   make board    other-m68k: schematic, ERC, netlist check, PDF, PCB from placement.json, DRC, DSN
#   make route    autoroute with freerouting under route_watch.py, import the SES, DRC again, unrouted count
#   make fab      Gerbers (+zip), drill, BOM, JLCPCB/PCBWay assembly files into build/fab (from the routed board)
#   make test     the small spike board (regression for gen_sch.py / gen_pcb.py)
#   make clean

PYTHON      ?= python3
KICAD_CLI   ?= kicad-cli
# freerouting: the 2.5.0 jar on a Java 25 runtime (~/.local/jre25, portable Temurin JRE; the
# 2.5.0 jar needs class-file version 69).  The 2.5.0 *native* CLI crashes in its routing
# thread after the first pass (GraalVM serialization of the board history) and then sleeps
# forever; the 2.1.0 jar runs on Java 21 but ignores the pass limit and never finishes.
# route_watch.py kills a run that shows no new pass and no CPU time for ROUTE_WATCH seconds.
JAVA        ?= $(HOME)/.local/jre25/bin/java
FREEROUTING_JAR ?= $(HOME)/.local/share/freerouting/freerouting-2.5.0.jar
FREEROUTING ?= $(JAVA) -jar $(FREEROUTING_JAR)
ROUTE_PASSES ?= 40
ROUTE_WATCH  ?= 900
# freerouting's own SMD fanout stage only adds dangling vias on this board (the GND/VCC
# fanout is already done by gen_pcb.py); keep it off
ROUTE_ARGS  ?= --router.autorouter.max_passes=$(ROUTE_PASSES) --router.fanout.enabled=false --router.optimizer.enabled=false
BUILD       := build

# --------------------------------------------------------------------------- the board
B    := $(BUILD)/other-m68k
BN   := other-m68k
FAB  := $(BUILD)/fab

.PHONY: board sch-board pcb-board route fab test sch pcb route-test clean

board: pcb-board

$(B)/$(BN).kicad_sch: board.py gen_sch.py parts.json lib/other-m68k.kicad_sym ../hdl/pins_cpld1_mmu.json ../hdl/pins_cpld2_io.json
	$(PYTHON) board.py $(B)

sch-board: $(B)/$(BN).kicad_sch
	$(KICAD_CLI) sch erc --format json --severity-all -o $(B)/erc.json $(B)/$(BN).kicad_sch
	$(PYTHON) -c "import json,sys; r=json.load(open('$(B)/erc.json')); n=sum(len(s['violations']) for s in r['sheets']); print('ERC violations:', n); sys.exit(1 if n else 0)"
	$(KICAD_CLI) sch export netlist --format kicadxml  -o $(B)/$(BN).xml $(B)/$(BN).kicad_sch
	$(KICAD_CLI) sch export netlist --format kicadsexpr -o $(B)/$(BN).net $(B)/$(BN).kicad_sch
	$(PYTHON) check_netlist.py board.py $(B)/$(BN).xml
	$(KICAD_CLI) sch export pdf -o $(B)/$(BN).pdf $(B)/$(BN).kicad_sch

pcb-board: sch-board placement.json
	$(PYTHON) gen_pcb.py build $(B)/$(BN).net $(B)/$(BN).kicad_pcb --placement placement.json
	$(KICAD_CLI) pcb drc --format json --severity-all -o $(B)/drc.json $(B)/$(BN).kicad_pcb
	$(PYTHON) -c "import json,sys; r=json.load(open('$(B)/drc.json')); print('DRC violations:', len(r['violations']), 'unconnected:', len(r['unconnected_items'])); [print(' ', v['type'], v['description'][:120]) for v in r['violations'][:20]]; sys.exit(1 if r['violations'] else 0)"
	$(PYTHON) gen_pcb.py dsn $(B)/$(BN).kicad_pcb $(B)/$(BN).dsn --power-layers In1.Cu,In2.Cu --plane-nets GND,VCC
	$(KICAD_CLI) pcb export svg --layers F.Cu,B.Cu,F.SilkS,Edge.Cuts,F.Fab --page-size-mode 2 -o $(B)/$(BN)-placement.svg $(B)/$(BN).kicad_pcb

route: pcb-board
	$(PYTHON) route_watch.py --log $(B)/route.log --interval $(ROUTE_WATCH) -- $(FREEROUTING) -de $(B)/$(BN).dsn -do $(B)/$(BN).ses $(ROUTE_ARGS)
	@grep -E 'pass #' $(B)/route.log | tail -3 | cut -c1-200
	$(PYTHON) gen_pcb.py ses $(B)/$(BN).kicad_pcb $(B)/$(BN).ses $(B)/$(BN)-routed.kicad_pcb
	cp $(B)/$(BN).kicad_pro $(B)/$(BN)-routed.kicad_pro
	$(KICAD_CLI) pcb drc --format json --severity-all -o $(B)/drc-routed.json $(B)/$(BN)-routed.kicad_pcb
	$(PYTHON) -c "import json,sys; r=json.load(open('$(B)/drc-routed.json')); print('DRC violations:', len(r['violations']), 'unrouted:', len(r['unconnected_items'])); [print(' ', v['type'], v['description'][:120]) for v in r['violations'][:20]]; sys.exit(1 if r['violations'] else 0)"
	$(KICAD_CLI) pcb export svg --layers F.Cu,B.Cu,In1.Cu,In2.Cu,Edge.Cuts --page-size-mode 2 -o $(B)/$(BN)-routed.svg $(B)/$(BN)-routed.kicad_pcb

# fab outputs come from the routed board when it exists, else from the placed one
FAB_PCB := $(if $(wildcard $(B)/$(BN)-routed.kicad_pcb),$(B)/$(BN)-routed.kicad_pcb,$(B)/$(BN).kicad_pcb)

fab:
	@test -f $(B)/$(BN).kicad_sch || $(MAKE) board
	rm -rf $(FAB) && mkdir -p $(FAB)
	$(KICAD_CLI) pcb export gerbers --no-protel-ext --subtract-soldermask --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,F.Paste,B.Paste,Edge.Cuts -o $(FAB)/ $(FAB_PCB)
	$(KICAD_CLI) pcb export drill --format excellon --excellon-separate-th --generate-map --map-format pdf -o $(FAB)/ $(FAB_PCB)
	$(KICAD_CLI) pcb export pos --format csv --units mm --side both --exclude-dnp -o $(FAB)/$(BN)-pos.csv $(FAB_PCB)
	$(KICAD_CLI) sch export bom --fields "Reference,Value,Footprint,\$${QUANTITY},\$${DNP},Manufacturer,MPN,LCSC,Assembly,Socket_MPN,Desc" --labels "Refs,Value,Footprint,Qty,DNP,Manufacturer,MPN,LCSC,Assembly,Socket MPN,Description" --group-by "Value,Footprint" -o $(FAB)/$(BN)-bom.csv $(B)/$(BN).kicad_sch
	$(PYTHON) fab_export.py $(B)/$(BN).xml $(FAB_PCB) $(FAB) --name $(BN)
	@echo "fab outputs in $(FAB)/ (from $(FAB_PCB))"; ls $(FAB)

# --------------------------------------------------------------------------- the spike
T    := $(BUILD)/test_small
NAME := test_small

test: sch pcb

$(T)/$(NAME).kicad_sch: test_small.py gen_sch.py
	$(PYTHON) test_small.py $(T)

sch: $(T)/$(NAME).kicad_sch
	$(KICAD_CLI) sch erc --format json --severity-all -o $(T)/erc.json $(T)/$(NAME).kicad_sch
	$(PYTHON) -c "import json,sys; r=json.load(open('$(T)/erc.json')); n=sum(len(s['violations']) for s in r['sheets']); print('ERC violations:', n); sys.exit(1 if n else 0)"
	$(KICAD_CLI) sch export netlist --format kicadxml  -o $(T)/$(NAME).xml $(T)/$(NAME).kicad_sch
	$(KICAD_CLI) sch export netlist --format kicadsexpr -o $(T)/$(NAME).net $(T)/$(NAME).kicad_sch
	$(PYTHON) check_netlist.py test_small.py $(T)/$(NAME).xml
	$(KICAD_CLI) sch export pdf -o $(T)/$(NAME).pdf $(T)/$(NAME).kicad_sch

pcb: sch
	$(PYTHON) gen_pcb.py build $(T)/$(NAME).net $(T)/$(NAME).kicad_pcb
	$(KICAD_CLI) pcb drc --format json --severity-all -o $(T)/drc.json $(T)/$(NAME).kicad_pcb
	$(PYTHON) -c "import json,sys; r=json.load(open('$(T)/drc.json')); print('DRC violations:', len(r['violations']), 'unconnected:', len(r['unconnected_items'])); sys.exit(1 if r['violations'] else 0)"
	$(PYTHON) gen_pcb.py dsn $(T)/$(NAME).kicad_pcb $(T)/$(NAME).dsn

route-test: pcb
	$(PYTHON) route_watch.py --log $(T)/route.log --interval $(ROUTE_WATCH) -- $(FREEROUTING) -de $(T)/$(NAME).dsn -do $(T)/$(NAME).ses --router.autorouter.max_passes=20 --router.fanout.enabled=false
	$(PYTHON) gen_pcb.py ses $(T)/$(NAME).kicad_pcb $(T)/$(NAME).ses $(T)/$(NAME)-routed.kicad_pcb
	cp $(T)/$(NAME).kicad_pro $(T)/$(NAME)-routed.kicad_pro
	$(KICAD_CLI) pcb drc --format json --severity-all -o $(T)/drc-routed.json $(T)/$(NAME)-routed.kicad_pcb
	$(PYTHON) -c "import json,sys; r=json.load(open('$(T)/drc-routed.json')); print('DRC violations:', len(r['violations']), 'unconnected:', len(r['unconnected_items'])); sys.exit(1 if r['violations'] or r['unconnected_items'] else 0)"

clean:
	rm -rf $(BUILD)
