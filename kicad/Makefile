# kicad/Makefile - generate and verify the test schematic/PCB with kicad-cli.
#
#   make test     generate test_small, run ERC, netlist check, PDF, PCB build, DRC, DSN export
#   make route    (optional) autoroute the test board with freerouting, import, DRC again
#   make clean

PYTHON     ?= python3
KICAD_CLI  ?= kicad-cli
FREEROUTING ?= $(HOME)/.local/share/freerouting/freerouting-cli
BUILD      := build
T          := $(BUILD)/test_small
NAME       := test_small

.PHONY: test sch pcb route clean

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

route: pcb
	$(FREEROUTING) -de $(T)/$(NAME).dsn -do $(T)/$(NAME).ses -mp 20
	$(PYTHON) gen_pcb.py ses $(T)/$(NAME).kicad_pcb $(T)/$(NAME).ses $(T)/$(NAME)-routed.kicad_pcb
	cp $(T)/$(NAME).kicad_pro $(T)/$(NAME)-routed.kicad_pro
	$(KICAD_CLI) pcb drc --format json --severity-all -o $(T)/drc-routed.json $(T)/$(NAME)-routed.kicad_pcb
	$(PYTHON) -c "import json,sys; r=json.load(open('$(T)/drc-routed.json')); print('DRC violations:', len(r['violations']), 'unconnected:', len(r['unconnected_items'])); sys.exit(1 if r['violations'] or r['unconnected_items'] else 0)"

clean:
	rm -rf $(BUILD)
