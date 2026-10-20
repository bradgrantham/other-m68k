#!/usr/bin/env python3
"""Harvest or check CPLD pin assignments.

  pins.py harvest <variant> <source.v>   copy build/fit/<variant>.pin into the source's
                                         pin block (lines starting with //PIN:) and write
                                         build/fit/<variant>.pins.json for the schematic
  pins.py check   <variant> <source.v>   fail if the block and the fitter's .pin differ

Only package pins (1..84) are kept; JTAG pins and buried nodes are dropped.  Bus bits are
named as yosys splits them: a[23:1] -> A_0..A_22 (A_n = A(n+1)), ma[16:0] -> ma_0..ma_16.
"""
import json, re, sys
JTAG = {"TDI", "TMS", "TCK", "TDO"}
MARK_BEGIN = "// ---- pin block (written by pins.py from a fitter run; see hdl/README.md) ----"
MARK_END   = "// ---- end pin block ----"

def read_pin_file(path):
    chip = None; pins = []
    for line in open(path):
        m = re.match(r'\s*CHIP\s+"([^"]+)"\s+ASSIGNED TO AN\s+(\S+)', line)
        if m: chip = (m.group(1), m.group(2)); continue
        m = re.match(r'\s*(\S+)\s*:\s*(\d+)\s*$', line)
        if not m: continue
        name, num = m.group(1), int(m.group(2))
        if 1 <= num <= 84 and name not in JTAG:
            pins.append((name, num))
    return chip, sorted(pins, key=lambda p: p[1])

def schematic_name(net, variant):
    """fitter net name -> board signal name"""
    m = re.match(r'^([A-Za-z]+(?:_n)?)_(\d+)$', net)
    if m:
        base, idx = m.group(1), int(m.group(2))
        if base.lower() == "a" and variant.startswith("cpld1"): return "A%d" % (idx + 1)
        if base.lower() == "a": return "A%d" % (idx + 1)      # cpld2 a[3:1]
        if base.lower() == "d": return "D%d" % idx
        if base == "ma": return "MA%d" % idx
        if base == "rgb": return ["B0", "B1", "G0", "G1", "G2", "R0", "R1", "R2"][idx]
        if base == "ipl_n": return "IPL%d" % idx
        if base == "uio": return "UIO%d" % idx
        if base == "led": return "LED%d" % idx
    return net.upper().replace("_N", "") if net.endswith("_n") else net.upper()

def block_text(chip, pins):
    out = [MARK_BEGIN, '//PIN: CHIP "%s" ASSIGNED TO AN %s' % chip]
    for name, num in pins:
        out.append("//PIN: %-14s : %d" % (name, num))
    out.append(MARK_END)
    return "\n".join(out) + "\n"

def current_block(src):
    m = re.search(re.escape(MARK_BEGIN) + r".*?" + re.escape(MARK_END) + r"\n", src, re.S)
    return m

def main():
    cmd, variant, source = sys.argv[1:4]
    pinfile = "build/fit/%s.pin" % variant
    chip, pins = read_pin_file(pinfile)
    src = open(source).read()
    if cmd == "harvest":
        blk = block_text(chip, pins)
        m = current_block(src)
        src = src[:m.start()] + blk + src[m.end():] if m else src.rstrip("\n") + "\n\n" + blk
        open(source, "w").write(src)
        json.dump({schematic_name(n, variant): p for n, p in pins}, open("pins_%s.json" % variant, "w"), indent=1, sort_keys=True)
        print("%s: %d pins written to %s and pins_%s.json" % (variant, len(pins), source, variant))
    elif cmd == "check":
        m = current_block(src)
        want = dict(pins)
        have = dict((a, int(b)) for a, b in re.findall(r"//PIN: (\S+)\s*:\s*(\d+)", m.group(0))) if m else {}
        if want != have:
            for k in sorted(set(want) | set(have)):
                if want.get(k) != have.get(k): print("  %-14s block=%s fitter=%s" % (k, have.get(k), want.get(k)))
            sys.exit("%s: pin block differs from the fitter result" % variant)
        print("%s: pin block matches the fitter (%d pins)" % (variant, len(pins)))

if __name__ == "__main__":
    main()
