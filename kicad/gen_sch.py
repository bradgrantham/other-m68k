#!/usr/bin/env python3
"""gen_sch.py - describe a board as parts and nets in Python, emit a KiCad 9 schematic.

Python 3 standard library only.  See kicad/README.md for the description API.

Overview of what is generated
  <name>.kicad_sch            root sheet (parts with sheet=None, plus sheet symbols)
  <name>-<sheet>.kicad_sch    one file per named sheet
  <name>.kicad_pro            minimal project so kicad-cli loads netclasses

Symbols are copied (flattened, "extends" resolved) from the system .kicad_sym libraries
into each sheet's lib_symbols.  Every connected pin gets a short wire stub from the pin
end outward and a global label carrying the net name; power nets may instead get a power
symbol at the stub end.  One power symbol + PWR_FLAG pair per power net anchors the net
for ERC.  Unconnected pins can be flagged with no-connects.
"""
import copy
import json
import math
import os
import re
import uuid

SYMBOL_DIR = os.environ.get("KICAD_SYMBOL_DIR", "/usr/share/kicad/symbols")
GRID = 1.27           # schematic grid; every pin end and wire end must sit on it
STUB = 3.81           # length of the wire from pin end to label (multiple of GRID)
LABEL_MARGIN_H = 30.0  # space reserved left/right of a symbol for labels
LABEL_MARGIN_V = 10.0  # space reserved above/below
POWER_STRIP = 20.0     # height of the strip holding power anchors on the first sheet
SHEET_BORDER = 15.0
PAPERS = [("A4", 297.0, 210.0), ("A3", 420.0, 297.0), ("A2", 594.0, 420.0),
          ("A1", 841.0, 594.0), ("A0", 1189.0, 841.0)]
UUID_NS = uuid.UUID("5e4a6b2c-0d3f-4c1e-9a8b-7f6e5d4c3b2a")


# ----------------------------------------------------------------------------- s-expressions
class Sym(str):
    """An unquoted atom (keyword, number, yes/no).  Plain str is a quoted string."""
    __slots__ = ()


def parse(text):
    """Parse one or more s-expressions; returns a list of top-level nodes."""
    i, n = 0, len(text)
    stack = [[]]
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif c == "(":
            stack.append([])
            i += 1
        elif c == ")":
            node = stack.pop()
            stack[-1].append(node)
            i += 1
        elif c == '"':
            j = i + 1
            out = []
            while text[j] != '"':
                if text[j] == "\\":
                    nxt = text[j + 1]
                    out.append({"n": "\n", "t": "\t", "r": "\r"}.get(nxt, nxt))
                    j += 2
                else:
                    out.append(text[j])
                    j += 1
            stack[-1].append("".join(out))
            i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in "()":
                j += 1
            stack[-1].append(Sym(text[i:j]))
            i = j
    if len(stack) != 1:
        raise ValueError("unbalanced parentheses")
    return stack[0]


def _quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def num(x):
    """Format a number the way KiCad does (no trailing zeros, no -0)."""
    if isinstance(x, (Sym, str)):
        return x
    if float(x) == int(x) and abs(x) < 1e9:
        return Sym(str(int(x)))
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    if s in ("-0", ""):
        s = "0"
    return Sym(s)


def dumps(node, depth=0):
    """Serialise in KiCad's style: atoms on the head line, sub-lists indented."""
    tab = "\t" * depth
    if not isinstance(node, list):
        return tab + (node if isinstance(node, Sym) else _quote(node))
    atoms, lists = [], []
    seen_list = False
    for item in node:
        if isinstance(item, list):
            seen_list = True
            lists.append(item)
        elif not seen_list:
            atoms.append(num(item) if isinstance(item, (int, float)) else item)
        else:
            # atom after a list: keep order by turning it into its own "line"
            lists.append(item)
    head = " ".join(a if isinstance(a, Sym) else _quote(a) for a in atoms)
    if not lists:
        return f"{tab}({head})"
    out = [f"{tab}({head}"]
    for item in lists:
        out.append(dumps(item, depth + 1))
    out.append(f"{tab})")
    return "\n".join(out)


def find(node, key):
    """First child list whose head is key."""
    for item in node:
        if isinstance(item, list) and item and item[0] == key:
            return item
    return None


def find_all(node, key):
    return [item for item in node if isinstance(item, list) and item and item[0] == key]


def L(*items):
    """Build a list node; bare python str heads/keywords are converted to Sym."""
    out = []
    for k, it in enumerate(items):
        if k == 0 and isinstance(it, str) and not isinstance(it, Sym):
            it = Sym(it)
        if isinstance(it, bool):
            it = Sym("yes" if it else "no")
        elif isinstance(it, (int, float)) and not isinstance(it, Sym):
            it = num(it)
        out.append(it)
    return out


def S(s):
    return Sym(s)


def grid(x):
    return round(round(x / GRID) * GRID, 4)


def det_uuid(*parts):
    return str(uuid.uuid5(UUID_NS, "/".join(str(p) for p in parts)))


# ----------------------------------------------------------------------------- symbol libraries
class Pin:
    __slots__ = ("number", "name", "etype", "x", "y", "angle", "length", "unit", "hidden")

    def __init__(self, number, name, etype, x, y, angle, length, unit, hidden):
        self.number, self.name, self.etype = number, name, etype
        self.x, self.y, self.angle, self.length = x, y, angle, length
        self.unit, self.hidden = unit, hidden

    def __repr__(self):
        return f"Pin({self.number} {self.name} {self.etype} u{self.unit})"


def clean_name(name):
    """'~{AS}' -> 'AS', 'R/~{W}' -> 'R/W', 'V_{CC}' -> 'VCC'."""
    return re.sub(r"[~_]\{([^}]*)\}", r"\1", name)


class LibSymbol:
    """A flattened library symbol: the s-expression node plus derived pin/bbox data."""

    def __init__(self, lib, name, node):
        self.lib, self.name, self.node = lib, name, node
        self.pins = []
        self.units = 1
        self.bbox = None
        self._scan()

    @property
    def lib_id(self):
        return f"{self.lib}:{self.name}"

    def is_power(self):
        return find(self.node, "power") is not None

    def _scan(self):
        xs, ys = [], []
        for sub in find_all(self.node, "symbol"):
            m = re.match(r"^(.*)_(\d+)_(\d+)$", sub[1])
            unit = int(m.group(2)) if m else 0
            self.units = max(self.units, unit)
            for item in sub[2:]:
                if not isinstance(item, list):
                    continue
                kind = item[0]
                if kind == "pin":
                    at = find(item, "at")
                    x, y = float(at[1]), float(at[2])
                    ang = float(at[3]) if len(at) > 3 else 0.0
                    length = float(find(item, "length")[1])
                    hide = find(item, "hide")
                    hidden = bool(hide and hide[1] == "yes") or Sym("hide") in item[1:3]
                    name = find(item, "name")[1]
                    number = find(item, "number")[1]
                    self.pins.append(Pin(number, name, str(item[1]), x, y, ang, length, unit, hidden))
                    dx, dy = _dir(ang)
                    xs += [x, x + dx * length]
                    ys += [y, y + dy * length]
                elif kind == "rectangle":
                    for k in ("start", "end"):
                        p = find(item, k)
                        xs.append(float(p[1])); ys.append(float(p[2]))
                elif kind in ("polyline", "bezier"):
                    for xy in find_all(find(item, "pts"), "xy"):
                        xs.append(float(xy[1])); ys.append(float(xy[2]))
                elif kind == "circle":
                    c = find(item, "center"); r = float(find(item, "radius")[1])
                    xs += [float(c[1]) - r, float(c[1]) + r]
                    ys += [float(c[2]) - r, float(c[2]) + r]
                elif kind == "arc":
                    for k in ("start", "mid", "end"):
                        p = find(item, k)
                        xs.append(float(p[1])); ys.append(float(p[2]))
                elif kind == "text":
                    p = find(item, "at")
                    xs.append(float(p[1])); ys.append(float(p[2]))
        for prop in find_all(self.node, "property"):
            if prop[1] in ("Reference", "Value"):
                at = find(prop, "at")
                xs.append(float(at[1])); ys.append(float(at[2]))
        if not xs:
            xs, ys = [0.0], [0.0]
        self.bbox = (min(xs), min(ys), max(xs), max(ys))

    def property(self, name):
        for prop in find_all(self.node, "property"):
            if prop[1] == name:
                return prop
        return None


def _dir(angle):
    """Unit vector (lib coords, y up) in which a pin of this angle points toward the body."""
    a = int(round(angle)) % 360
    return {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}[a]


class SymbolLibs:
    _files = {}
    _flat = {}

    @classmethod
    def _load(cls, lib):
        if lib not in cls._files:
            path = os.path.join(SYMBOL_DIR, lib + ".kicad_sym")
            with open(path, encoding="utf-8") as f:
                top = parse(f.read())[0]
            cls._files[lib] = {s[1]: s for s in find_all(top, "symbol")}
        return cls._files[lib]

    @classmethod
    def names(cls, lib):
        return list(cls._load(lib).keys())

    @classmethod
    def get(cls, lib, name):
        key = (lib, name)
        if key not in cls._flat:
            node = cls._flatten(lib, name)
            node[1] = f"{lib}:{name}"
            cls._flat[key] = LibSymbol(lib, name, node)
        return cls._flat[key]

    @classmethod
    def _flatten(cls, lib, name):
        syms = cls._load(lib)
        if name not in syms:
            raise KeyError(f"symbol {name!r} not in library {lib}")
        node = copy.deepcopy(syms[name])
        ext = find(node, "extends")
        if ext is None:
            return node
        parent = cls._flatten(lib, ext[1])
        # start from the parent, override properties with the child's, rename sub-units
        out = copy.deepcopy(parent)
        out[1] = name
        for prop in find_all(node, "property"):
            existing = None
            for p in find_all(out, "property"):
                if p[1] == prop[1]:
                    existing = p
            if existing is not None:
                out[out.index(existing)] = copy.deepcopy(prop)
            else:
                # insert before the first sub-symbol
                idx = next((i for i, it in enumerate(out) if isinstance(it, list) and it[0] == "symbol"), len(out))
                out.insert(idx, copy.deepcopy(prop))
        for item in node:
            if isinstance(item, list) and item[0] not in ("property", "extends") and find(out, item[0]) is None:
                out.insert(2, copy.deepcopy(item))
        for sub in find_all(out, "symbol"):
            m = re.match(r"^(.*)_(\d+)_(\d+)$", sub[1])
            sub[1] = f"{name}_{m.group(2)}_{m.group(3)}"
        return out


# ----------------------------------------------------------------------------- board model
class PartPin:
    """A pin of a placed part."""
    __slots__ = ("part", "pin")

    def __init__(self, part, pin):
        self.part, self.pin = part, pin

    @property
    def key(self):
        return (self.part.ref, self.pin.number)

    def __repr__(self):
        return f"{self.part.ref}.{self.pin.number}({self.pin.name})"


class Part:
    def __init__(self, board, ref, lib, symbol, footprint, value, sheet, place, props):
        self.board, self.ref = board, ref
        self.libsym = SymbolLibs.get(lib, symbol)
        self.footprint = footprint or ""
        self.value = value if value is not None else symbol
        self.sheet = sheet
        self.place = place or {}      # placement hints: row, col, x, y (schematic mm)
        self.props = props            # extra properties (Datasheet, MPN, ...)
        self.nc_pins = set()          # pin numbers flagged no-connect
        self.nc_unused = False

    # -- pin lookup
    def _match(self, key):
        key = str(key)
        hits = [p for p in self.libsym.pins if p.number == key]
        if not hits:
            hits = [p for p in self.libsym.pins if p.name == key]
        if not hits:
            hits = [p for p in self.libsym.pins if clean_name(p.name) == key]
        return hits

    def pin(self, key):
        hits = self._match(key)
        if not hits:
            raise KeyError(f"{self.ref}: no pin {key!r}; pins are "
                           + ", ".join(f"{p.number}:{clean_name(p.name)}" for p in self.libsym.pins))
        if len(hits) > 1:
            raise KeyError(f"{self.ref}: pin {key!r} is ambiguous ({[p.number for p in hits]}); use .pins()")
        return PartPin(self, hits[0])

    def pins(self, *keys):
        out = []
        for key in keys:
            hits = self._match(key)
            if not hits:
                raise KeyError(f"{self.ref}: no pin {key!r}")
            out += [PartPin(self, p) for p in hits]
        return out

    def all_pins(self):
        return [PartPin(self, p) for p in self.libsym.pins]

    def nc(self, *keys):
        for pp in self.pins(*keys):
            self.nc_pins.add(pp.pin.number)
        return self

    def __repr__(self):
        return f"Part({self.ref} {self.libsym.lib_id})"


class Net:
    def __init__(self, name):
        self.name = name
        self.pins = []
        self.power = False
        self.power_symbol = None   # e.g. "power:VCC"

    def add(self, *pins):
        for p in pins:
            if isinstance(p, (list, tuple)):
                self.add(*p)
            else:
                self.pins.append(p)
        return self


class Board:
    def __init__(self, name, title=None, company=None, rev="A"):
        self.name = name
        self.title = title or name
        self.company = company or ""
        self.rev = rev
        self.parts = {}
        self.nets = {}
        self.sheet_order = []        # named sheets in creation order
        self.sheet_titles = {}

    # -- description API
    def sheet(self, name, title=None):
        if name not in self.sheet_order:
            self.sheet_order.append(name)
        self.sheet_titles[name] = title or name
        return name

    def part(self, ref, lib, symbol, footprint=None, value=None, sheet=None, place=None, **props):
        if ref in self.parts:
            raise ValueError(f"duplicate reference {ref}")
        if sheet is not None:
            self.sheet(sheet)
        p = Part(self, ref, lib, symbol, footprint, value, sheet, place, props)
        self.parts[ref] = p
        return p

    def net(self, name, *pins):
        n = self.nets.get(name)
        if n is None:
            n = self.nets[name] = Net(name)
        n.add(*pins)
        return n

    def power(self, name, *pins, symbol=None):
        n = self.net(name, *pins)
        n.power = True
        if symbol is not None:
            n.power_symbol = symbol
        elif n.power_symbol is None:
            n.power_symbol = "power:" + name if name in SymbolLibs.names("power") else None
        return n

    def bus(self, name, lo, hi, *members, fmt="{name}{i}"):
        """Nets name<lo>..name<hi>.  Members: Part (pin named like the net), or
        (Part, "A{}") pin-name template, or (Part, "A{}", offset) with the index shifted."""
        nets = []
        for i in range(lo, hi + 1):
            netname = fmt.format(name=name, i=i)
            pins = []
            for m in members:
                if isinstance(m, Part):
                    pins.append(m.pin(netname))
                else:
                    part, tmpl = m[0], m[1]
                    off = m[2] if len(m) > 2 else 0
                    pins.append(part.pin(tmpl.format(i + off)))
            nets.append(self.net(netname, *pins))
        return nets

    def nc(self, *pins):
        for p in pins:
            if isinstance(p, (list, tuple)):
                self.nc(*p)
            else:
                p.part.nc_pins.add(p.pin.number)

    # -- queries
    def pin_net(self):
        """{(ref, number): Net}; checks no pin is in two nets."""
        m = {}
        for n in self.nets.values():
            for pp in n.pins:
                if pp.key in m and m[pp.key] is not n:
                    raise ValueError(f"pin {pp} is in nets {m[pp.key].name} and {n.name}")
                m[pp.key] = n
        return m

    def expected_nets(self):
        """{netname: {(ref, pin number)}} for comparison against the exported netlist."""
        return {n.name: {pp.key for pp in n.pins} for n in self.nets.values() if n.pins}

    def check(self):
        """Validate; returns a list of warning strings (unconnected pins without NC)."""
        warnings = []
        pin_net = self.pin_net()
        for n in self.nets.values():
            if len(n.pins) == 1 and not n.power:
                warnings.append(f"net {n.name} has a single pin {n.pins[0]}")
        for part in self.parts.values():
            # pins stacked at the same location must share a net
            by_loc = {}
            for pin in part.libsym.pins:
                by_loc.setdefault((pin.unit, pin.x, pin.y), []).append(pin)
            for loc, pins in by_loc.items():
                nets = {pin_net.get((part.ref, p.number)) for p in pins}
                nets.discard(None)
                if len(nets) > 1:
                    raise ValueError(f"{part.ref}: stacked pins {pins} are on different nets")
                if len(nets) == 1 and len(pins) > 1:
                    missing = [p for p in pins if (part.ref, p.number) not in pin_net]
                    for p in missing:
                        raise ValueError(f"{part.ref}: pin {p.number} is stacked on a connected pin; "
                                         f"connect it to {next(iter(nets)).name} too")
            for pin in part.libsym.pins:
                k = (part.ref, pin.number)
                if k not in pin_net and pin.number not in part.nc_pins and not pin.hidden:
                    if part.nc_unused:
                        part.nc_pins.add(pin.number)
                    else:
                        warnings.append(f"{part.ref} pin {pin.number} ({clean_name(pin.name)}) unconnected")
                if k in pin_net and pin.hidden:
                    stacked = [p for p in by_loc[(pin.unit, pin.x, pin.y)] if not p.hidden]
                    if not stacked:
                        warnings.append(f"{part.ref} pin {pin.number} ({clean_name(pin.name)}) is hidden and not "
                                        f"stacked on a visible pin; KiCad will connect it implicitly to a net "
                                        f"named {pin.name!r}, not to {pin_net[k].name}")
        return warnings

    # -- generation
    def write(self, outdir):
        gen = Generator(self)
        return gen.write(outdir)


# ----------------------------------------------------------------------------- generation
class Instance:
    """One placed symbol unit on a sheet."""

    def __init__(self, part, unit, libsym, value, ref):
        self.part, self.unit, self.libsym = part, unit, libsym
        self.value, self.ref = value, ref
        self.x = self.y = 0.0
        self.uuid = None
        self.sheet = None

    def cell(self):
        x0, y0, x1, y1 = self.libsym.bbox
        w = (x1 - x0) + 2 * LABEL_MARGIN_H
        h = (y1 - y0) + 2 * LABEL_MARGIN_V
        return w, h

    def to_sheet(self, lx, ly):
        """Library coordinates (y up) -> sheet coordinates (y down)."""
        return (round(self.x + lx, 4), round(self.y - ly, 4))


class Sheet:
    def __init__(self, name, filename, title):
        self.name, self.filename, self.title = name, filename, title
        self.instances = []
        self.items = []          # extra s-expression nodes (wires, labels, no_connects, power symbols)
        self.lib_symbols = {}    # lib_id -> node
        self.paper = ("A4", 297.0, 210.0)
        self.uuid = None
        self.sheet_uuid = None   # uuid of the (sheet) element in the root (None for root)


class Generator:
    def __init__(self, board):
        self.board = board
        self.warnings = board.check()
        self.sheets = []
        self.root = None
        self.power_symbol_count = 0

    # ---------------------------------------------------------------- layout
    def _make_sheets(self):
        b = self.board
        root_uuid = det_uuid(b.name, "root")
        self.root = Sheet(None, f"{b.name}.kicad_sch", b.title)
        self.root.uuid = root_uuid
        self.sheets = [self.root]
        for i, name in enumerate(b.sheet_order):
            sh = Sheet(name, f"{b.name}-{name}.kicad_sch", b.sheet_titles.get(name, name))
            sh.uuid = det_uuid(b.name, "sheet", name)
            sh.sheet_uuid = det_uuid(b.name, "sheetsym", name)
            self.sheets.append(sh)
        by_name = {sh.name: sh for sh in self.sheets}
        for part in b.parts.values():
            ls = part.libsym
            for unit in range(1, ls.units + 1):
                inst = Instance(part, unit, ls, part.value, part.ref)
                inst.uuid = det_uuid(b.name, "inst", part.ref, unit)
                sh = by_name[part.sheet]
                inst.sheet = sh
                sh.instances.append(inst)

    def _layout_sheet(self, sh, reserve_top):
        """Deterministic row packing: biggest cells first, rows filled left to right."""
        insts = sorted(sh.instances, key=lambda i: (-i.cell()[1], -i.cell()[0], i.ref))
        total_area = sum(w * h for w, h in (i.cell() for i in insts))
        target_w = max(260.0, math.sqrt(total_area * 1.6))
        rows, row, row_w = [], [], 0.0
        for inst in insts:
            w, h = inst.cell()
            if row and row_w + w > target_w:
                rows.append(row)
                row, row_w = [], 0.0
            row.append(inst)
            row_w += w
        if row:
            rows.append(row)
        y = SHEET_BORDER + reserve_top
        max_w = 0.0
        for row in rows:
            x = SHEET_BORDER
            row_h = max(i.cell()[1] for i in row)
            for inst in row:
                w, h = inst.cell()
                x0, y0, x1, y1 = inst.libsym.bbox
                # symbol origin so that its bbox (plus margins) sits inside the cell
                inst.x = grid(x + LABEL_MARGIN_H - x0)
                inst.y = grid(y + LABEL_MARGIN_V + y1)
                x += w
            max_w = max(max_w, x)
            y += row_h
        need_w, need_h = max_w + SHEET_BORDER, y + SHEET_BORDER
        for name, w, h in PAPERS:
            if w >= need_w and h >= need_h:
                sh.paper = (name, w, h)
                break
        else:
            sh.paper = ("User", math.ceil(need_w), math.ceil(need_h))

    # ---------------------------------------------------------------- nodes
    def _effects(self, size=1.27, justify=None, hide=False):
        e = L("effects", L("font", L("size", size, size)))
        if justify:
            e.append(L("justify", *[S(j) for j in justify.split()]))
        if hide:
            e.append(L("hide", True))
        return e

    def _prop(self, name, value, x, y, angle=0, effects=None, hide=False):
        if effects is None:
            effects = self._effects(hide=hide)
        return L("property", name, value, L("at", x, y, angle), effects)

    def _wire(self, sh, p0, p1, tag):
        sh.items.append(L("wire", L("pts", L("xy", p0[0], p0[1]), L("xy", p1[0], p1[1])),
                          L("stroke", L("width", 0), L("type", S("default"))),
                          L("uuid", det_uuid(self.board.name, "wire", tag))))

    def _global_label(self, sh, text, pos, outward, shape, tag):
        # outward: direction from the pin into free space, in sheet coords
        angle, justify = {(-1, 0): (180, "right"), (1, 0): (0, "left"),
                          (0, 1): (270, "right"), (0, -1): (90, "left")}[outward]
        sh.items.append(L("global_label", text, L("shape", S(shape)),
                          L("at", pos[0], pos[1], angle), L("fields_autoplaced", True),
                          self._effects(1.27, justify),
                          L("uuid", det_uuid(self.board.name, "glabel", tag)),
                          L("property", "Intersheetrefs", "${INTERSHEET_REFS}",
                            L("at", pos[0], pos[1], 0), self._effects(1.27, justify, hide=True))))

    def _no_connect(self, sh, pos, tag):
        sh.items.append(L("no_connect", L("at", pos[0], pos[1]),
                          L("uuid", det_uuid(self.board.name, "nc", tag))))

    def _symbol_node(self, sh, libsym, ref, value, x, y, angle, unit, inst_uuid, footprint="", props=None):
        """A placed symbol instance node, registering the lib symbol on the sheet."""
        sh.lib_symbols.setdefault(libsym.lib_id, libsym.node)
        node = L("symbol", L("lib_id", libsym.lib_id), L("at", x, y, angle), L("unit", unit),
                 L("exclude_from_sim", False), L("in_bom", not libsym.is_power()),
                 L("on_board", not libsym.is_power()), L("dnp", False),
                 L("uuid", inst_uuid))

        def lib_prop_pos(name, default_dy):
            p = libsym.property(name)
            if p is None:
                return (x, y + default_dy, 0, None)
            at = find(p, "at")
            eff = copy.deepcopy(find(p, "effects")) if find(p, "effects") else None
            return (round(x + float(at[1]), 4), round(y - float(at[2]), 4), float(at[3]) if len(at) > 3 else 0, eff)

        for name, val, hide_default in (("Reference", ref, False), ("Value", value, False),
                                        ("Footprint", footprint, True), ("Datasheet", "", True),
                                        ("Description", "", True)):
            px, py, pa, eff = lib_prop_pos(name, 0)
            lp = libsym.property(name)
            if name in ("Datasheet", "Description") and lp is not None and val == "":
                val = lp[2]
            if props and name in props:
                val = props[name]
            if eff is None:
                eff = self._effects(hide=hide_default)
            node.append(L("property", name, val, L("at", px, py, pa), eff))
        if props:
            for k, v in props.items():
                if k not in ("Reference", "Value", "Footprint", "Datasheet", "Description"):
                    node.append(L("property", k, str(v), L("at", x, y, 0), self._effects(hide=True)))
        for pin in libsym.pins:
            node.append(L("pin", pin.number, L("uuid", det_uuid(self.board.name, "pin", inst_uuid, pin.number))))
        path = "/" + self.root.uuid + ("" if sh is self.root else "/" + sh.sheet_uuid)
        node.append(L("instances", L("project", self.board.name,
                                     L("path", path, L("reference", ref), L("unit", unit)))))
        return node

    # ---------------------------------------------------------------- wiring
    def _wire_instances(self, sh):
        pin_net = self.board.pin_net()
        for inst in sh.instances:
            part = inst.part
            done_locs = set()
            for pin in inst.libsym.pins:
                if pin.unit not in (0, inst.unit) or pin.hidden:
                    continue
                loc = (pin.x, pin.y)
                if loc in done_locs:
                    continue
                done_locs.add(loc)
                end = inst.to_sheet(pin.x, pin.y)
                dx, dy = _dir(pin.angle)
                outward = (-dx, dy)        # flip x (opposite of body), y axis inverted
                net = pin_net.get((part.ref, pin.number))
                tag = f"{part.ref}/{pin.number}"
                if net is None:
                    if pin.number in part.nc_pins:
                        self._no_connect(sh, end, tag)
                    continue
                stub_end = (round(end[0] + outward[0] * STUB, 4), round(end[1] + outward[1] * STUB, 4))
                self._wire(sh, end, stub_end, tag)
                if net.power and self._power_symbol_fits(net, outward):
                    self._place_power_symbol(sh, net, stub_end, tag)
                else:
                    self._global_label(sh, net.name, stub_end, outward, label_shape(pin.etype), tag)

    def _power_libsym(self, net):
        if not net.power_symbol:
            return None
        lib, name = net.power_symbol.split(":", 1)
        return SymbolLibs.get(lib, name)

    def _power_symbol_fits(self, net, outward):
        ls = self._power_libsym(net)
        if ls is None or len(ls.pins) != 1:
            return False
        a = int(round(ls.pins[0].angle)) % 360
        # power:VCC has its pin at angle 90 (points up into a body drawn above the origin), so
        # it sits at the end of a stub leaving the part upward; power:GND is angle 270 (body below)
        return (a == 90 and outward == (0, -1)) or (a == 270 and outward == (0, 1))

    def _place_power_symbol(self, sh, net, pos, tag, with_flag=False):
        ls = self._power_libsym(net)
        self.power_symbol_count += 1
        ref = f"#PWR{self.power_symbol_count:03d}"
        iu = det_uuid(self.board.name, "pwr", tag)
        sh.items.append(self._symbol_node(sh, ls, ref, net.name, pos[0], pos[1], 0, 1, iu))
        if with_flag:
            flag = SymbolLibs.get("power", "PWR_FLAG")
            fpos = (round(pos[0] + 10.16, 4), pos[1])
            self._wire(sh, pos, fpos, tag + "/flag")
            fref = f"#FLG{self.power_symbol_count:03d}"
            sh.items.append(self._symbol_node(sh, flag, fref, "PWR_FLAG", fpos[0], fpos[1], 0, 1,
                                              det_uuid(self.board.name, "flag", tag)))

    def _power_anchors(self, sh):
        """One power symbol + PWR_FLAG per power net, in a strip at the top of the sheet."""
        x = SHEET_BORDER + 5.08
        y = grid(SHEET_BORDER + POWER_STRIP - 5.08)
        for net in self.board.nets.values():
            if not net.power or not net.pins:
                continue
            if self._power_libsym(net) is None:
                # no power symbol: use a plain label + PWR_FLAG
                flag = SymbolLibs.get("power", "PWR_FLAG")
                pos = (grid(x), y)
                self._global_label(sh, net.name, pos, (-1, 0), "passive", f"anchor/{net.name}")
                self._wire(sh, pos, (grid(x + 5.08), y), f"anchor/{net.name}")
                self.power_symbol_count += 1
                sh.items.append(self._symbol_node(sh, flag, f"#FLG{self.power_symbol_count:03d}", "PWR_FLAG",
                                                  grid(x + 5.08), y, 0, 1,
                                                  det_uuid(self.board.name, "flag", "anchor", net.name)))
            else:
                self._place_power_symbol(sh, net, (grid(x), y), f"anchor/{net.name}", with_flag=True)
            x += 30.48

    # ---------------------------------------------------------------- output
    def _sheet_header(self, sh, page_no):
        tb = L("title_block", L("title", sh.title), L("rev", self.board.rev))
        if self.board.company:
            tb.append(L("company", self.board.company))
        tb.append(L("comment", 1, "generated by kicad/gen_sch.py - do not edit"))
        paper = L("paper", sh.paper[0]) if sh.paper[0] != "User" else L("paper", "User", sh.paper[1], sh.paper[2])
        return [L("version", S("20250114")), L("generator", "gen_sch"), L("generator_version", "9.0"),
                L("uuid", sh.uuid), paper, tb]

    def _sheet_symbol(self, sh, idx, page_no):
        """The (sheet ...) element in the root pointing at a child sheet file."""
        x = grid(SHEET_BORDER + 5.08 + idx * 40.64)
        y = grid(SHEET_BORDER + POWER_STRIP + 10.16)
        w, h = 30.48, 20.32
        return L("sheet", L("at", x, y), L("size", w, h), L("exclude_from_sim", False),
                 L("in_bom", True), L("on_board", True), L("dnp", False), L("fields_autoplaced", True),
                 L("stroke", L("width", 0.1524), L("type", S("solid"))),
                 L("fill", L("color", 0, 0, 0, S("0.0000"))),
                 L("uuid", sh.sheet_uuid),
                 L("property", "Sheetname", sh.name, L("at", x, round(y - 0.7116, 4), 0),
                   self._effects(1.27, "left bottom")),
                 L("property", "Sheetfile", sh.filename, L("at", x, round(y + h + 0.5846, 4), 0),
                   self._effects(1.27, "left top")),
                 L("instances", L("project", self.board.name, L("path", "/" + self.root.uuid, L("page", str(page_no))))))

    def _write_lib_tables(self, outdir, written):
        """Project-local sym-lib-table / fp-lib-table so kicad-cli resolves libraries without
        any user configuration (a fresh account has no ~/.config/kicad/9.0/sym-lib-table)."""
        sym_libs = sorted({lid.split(":", 1)[0] for sh in self.sheets for lid in sh.lib_symbols})
        fp_libs = sorted({p.footprint.split(":", 1)[0] for p in self.board.parts.values() if ":" in p.footprint})
        for fname, head, libs, var, ext in (("sym-lib-table", "sym_lib_table", sym_libs, "KICAD9_SYMBOL_DIR", ".kicad_sym"),
                                             ("fp-lib-table", "fp_lib_table", fp_libs, "KICAD9_FOOTPRINT_DIR", ".pretty")):
            path = os.path.join(outdir, fname)
            with open(path, "w", encoding="utf-8") as f:
                f.write(f"({head}\n  (version 7)\n")
                for lib in libs:
                    f.write(f'  (lib (name "{lib}")(type "KiCad")(uri "${{{var}}}/{lib}{ext}")(options "")(descr ""))\n')
                f.write(")\n")
            written.append(path)

    def write(self, outdir):
        os.makedirs(outdir, exist_ok=True)
        self._make_sheets()
        first_with_parts = next((sh for sh in self.sheets if sh.instances), self.root)
        for sh in self.sheets:
            reserve = POWER_STRIP if sh is first_with_parts else 0.0
            if sh is self.root and len(self.sheets) > 1:
                reserve += 40.0   # room for the sheet symbols
            self._layout_sheet(sh, reserve)
            if sh is first_with_parts:
                self._power_anchors(sh)
            for inst in sh.instances:
                sh.items.insert(0, self._symbol_node(sh, inst.libsym, inst.ref, inst.value, inst.x, inst.y, 0,
                                                     inst.unit, inst.uuid, inst.part.footprint, inst.part.props))
            self._wire_instances(sh)
        written = []
        for page_no, sh in enumerate(self.sheets, start=1):
            doc = L("kicad_sch", *self._sheet_header(sh, page_no))
            lib = L("lib_symbols")
            for lib_id in sorted(sh.lib_symbols):
                lib.append(sh.lib_symbols[lib_id])
            doc.append(lib)
            if sh is self.root:
                for idx, child in enumerate(self.sheets[1:]):
                    doc.append(self._sheet_symbol(child, idx, idx + 2))
            # KiCad writes items grouped by type; order does not matter for loading
            order = {"junction": 0, "no_connect": 1, "wire": 2, "global_label": 3, "symbol": 4}
            for item in sorted(sh.items, key=lambda it: order.get(it[0], 9)):
                doc.append(item)
            if sh is self.root:
                doc.append(L("sheet_instances", L("path", "/", L("page", "1"))))
            doc.append(L("embedded_fonts", False))
            path = os.path.join(outdir, sh.filename)
            with open(path, "w", encoding="utf-8") as f:
                f.write(dumps(doc) + "\n")
            written.append(path)
        self._write_lib_tables(outdir, written)
        pro = os.path.join(outdir, self.board.name + ".kicad_pro")
        with open(pro, "w", encoding="utf-8") as f:
            json.dump(project_json(self.board.name), f, indent=2)
            f.write("\n")
        written.append(pro)
        return written


def label_shape(etype):
    return {"input": "input", "output": "output", "bidirectional": "bidirectional",
            "tri_state": "tri_state", "passive": "passive", "power_in": "passive",
            "power_out": "passive", "open_collector": "passive", "open_emitter": "passive",
            "unspecified": "passive", "free": "passive", "no_connect": "passive"}.get(etype, "passive")


def project_json(name):
    """A minimal .kicad_pro: netclass defaults so kicad-cli pcb drc has rules to apply."""
    return {
        "board": {"design_settings": {"defaults": {}, "rule_severities": {}, "rules": {
            "min_clearance": 0.2, "min_track_width": 0.2, "min_via_diameter": 0.6,
            "min_via_annular_width": 0.15, "min_through_hole_diameter": 0.3, "min_hole_clearance": 0.25,
            "min_copper_edge_clearance": 0.5, "solder_mask_clearance": 0.0, "solder_mask_min_width": 0.0,
            "min_hole_to_hole": 0.25, "min_microvia_diameter": 0.2, "min_microvia_drill": 0.1,
            "min_resolved_spokes": 2, "min_silk_clearance": 0.0, "min_text_height": 0.8,
            "min_text_thickness": 0.08, "use_height_for_length_calcs": True,
            "max_error": 0.005, "allow_blind_buried_vias": False, "allow_microvias": False},
            "meta": {"version": 2}}, "layer_presets": [], "viewports": []},
        "boards": [], "cvpcb": {"equivalence_files": []}, "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": f"{name}.kicad_pro", "version": 3},
        "net_settings": {"classes": [{
            "bus_width": 12, "clearance": 0.25, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
            "diff_pair_width": 0.2, "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1,
            "name": "Default", "pcb_color": "rgba(0, 0, 0, 0.000)", "priority": 2147483647,
            "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": 0.3, "via_diameter": 0.8,
            "via_drill": 0.4, "wire_width": 6}],
            "meta": {"version": 4}, "net_colors": None, "netclass_assignments": None, "netclass_patterns": []},
        "pcbnew": {"last_paths": {"gencad": "", "idf": "", "netlist": "", "plot": "", "pos_files": "",
                                  "specctra_dsn": "", "step": "", "svg": "", "vrml": ""}, "page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [], "text_variables": {},
    }
