#!/usr/bin/env python3
"""check_netlist.py - compare a kicad-cli XML netlist with a Board description.

    python3 check_netlist.py <board_module.py> <netlist.xml>

The module must define build() returning a gen_sch.Board.  Exit status 1 on mismatch.
"""
import importlib.util
import os
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def load_board(path):
    spec = importlib.util.spec_from_file_location("board_desc", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.build()


def read_xml_nets(path):
    root = ET.parse(path).getroot()
    nets = {}
    for net in root.find("nets"):
        name = net.get("name")
        nodes = {(n.get("ref"), n.get("pin")) for n in net.findall("node") if not n.get("ref").startswith("#")}
        if name.startswith("unconnected-"):
            continue
        nets[name] = nodes
    return nets


def main():
    board = load_board(sys.argv[1])
    expected = board.expected_nets()
    actual = read_xml_nets(sys.argv[2])
    ok = True
    for name in sorted(set(expected) | set(actual)):
        e, a = expected.get(name), actual.get(name)
        if e is None:
            print(f"UNEXPECTED net {name}: {sorted(a)}"); ok = False
        elif a is None:
            print(f"MISSING net {name}: expected {sorted(e)}"); ok = False
        elif e != a:
            print(f"MISMATCH net {name}: missing {sorted(e - a)}, extra {sorted(a - e)}"); ok = False
    pins_total = sum(len(v) for v in expected.values())
    print(f"{'OK' if ok else 'FAIL'}: {len(expected)} nets, {pins_total} pins described; {len(actual)} nets in netlist")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
