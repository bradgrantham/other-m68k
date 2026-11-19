#!/usr/bin/env python3
"""Static-constructor test: boot build/test/rom_ctors.bin (the BIOS plus test/ctors.c and
test/ctors_cxx.cpp) in the emulator and check the record the constructors left in BIOS RAM.

usage: ctors.py EMULATOR NM ROM.elf ROM.bin
Prints PASS:/FAIL: lines and ALL TESTS PASSED; exit status 1 on any failure."""
import re
import struct
import subprocess
import sys

DATA_PROBE = 0x600DDA7A         # CTOR_DATA_PROBE in ctors.h
LOG_SIZE = 56                   # struct ctor_log
ORDER = "P123cXY"               # see ctors.c; L (the .ctors entry) is checked apart
TYPE_AT = 350                   # frames: the POST and the banner take about 300
DUMPS = [("ctor_log", LOG_SIZE), ("ctor_probe_3", 8), ("ctor_probe_x", 8), ("ctor_probe_y", 8)]

fails = 0


def check(what, ok, detail=""):
    global fails
    print("%s: %s%s" % ("PASS" if ok else "FAIL", what, " (%s)" % detail if detail else ""))
    fails += not ok


class Boot:
    """One headless run: `typed` goes to the keyboard at frame TYPE_AT, memory is read at exit."""

    def __init__(self, emu, rom, sym, typed, frames):
        cmd = [emu, "--rom", rom, "--headless", "--fast", "--no-stdin", "--exit-on-berr",
               "--frames", str(frames), "--type", typed, "--type-delay", str(TYPE_AT), "--text-dump", "-"]
        for name, size in DUMPS:
            cmd += ["--dump-mem", "0x%x" % sym[name], str(size)]
        run = subprocess.run(cmd, capture_output=True)
        self.sym = sym
        self.status = run.returncode
        self.out = run.stdout.decode("latin-1")         # serial output, then the console text
        self.err = run.stderr.decode("latin-1")
        summary = [l for l in self.err.splitlines() if " frames, " in l]
        self.summary = summary[0] if summary else "no summary line"
        self.mem = {}           # address -> byte, from the "00ABCD: xx xx ..  |....|" lines
        for line in self.err.splitlines():
            m = re.match(r"^([0-9A-F]{6}):((?: [0-9A-F]{2}|   ){16})  \|", line)
            if m:
                base = int(m.group(1), 16)
                for i in range(16):
                    b = m.group(2)[3 * i + 1:3 * i + 3]
                    if b.strip():
                        self.mem[base + i] = int(b, 16)

    def peek(self, name, size):
        return bytes(self.mem.get(self.sym[name] + i, 0xEE) for i in range(size))

    def log(self):
        f = struct.unpack(">16sHHIIIIHH16s", self.peek("ctor_log", LOG_SIZE))
        return (f[0].rstrip(b"\0").decode("latin-1"),) + f[1:9] + (f[9].split(b"\0")[0].decode("latin-1"),)


def main():
    emu, nm, elf, rom = sys.argv[1:5]
    sym = {}
    for line in subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if len(f) == 3:
            sym[f[2]] = int(f[0], 16)

    # PROVIDE'd by rom.ld only when crt0 refers to them
    missing = [s for s in ["__preinit_array_start", "__preinit_array_end", "__init_array_start", "__init_array_end"]
               + [d[0] for d in DUMPS] if s not in sym]
    check("the ROM has the table bounds and the test objects", not missing, "missing: " + " ".join(missing) if missing else "")
    if missing:
        print("%d FAILED" % fails)
        return 1
    pre = (sym["__preinit_array_end"] - sym["__preinit_array_start"]) // 4
    ini = (sym["__init_array_end"] - sym["__init_array_start"]) // 4
    check(".preinit_array holds the test's entry", pre >= 1, "%d entries" % pre)
    check(".init_array holds the test's 6 entries", ini >= 6, "%d entries" % ini)
    # .fini_array is discarded, so the C destructor is garbage-collected; the C++ one stays
    # because the constructor passes its address to __cxa_atexit
    check("the __attribute__((destructor)) function was dropped at link time", "never" not in sym)

    print("== cold boot")
    b = Boot(emu, rom, sym, "info\\n", 500)
    check("emulator exit status 0", b.status == 0, "got %d" % b.status)
    check("no bus errors", ", 0 bus errors" in b.summary, b.summary)
    order, n, sr, data_seen, con_fb_seen, cxx_value, cxx_this, serial_seen, dtors, cxx_text = b.log()
    check("constructors ran in order %s" % ORDER, order.replace("L", "") == ORDER, "ran %r" % order)
    check("the .ctors entry ran once, among the plain ones",
          order.count("L") == 1 and "3" in order and order.index("L") > order.index("3"), "ran %r" % order)
    check("each ran exactly once", n == len(ORDER) + 1, "%d calls" % n)
    check("supervisor mode, interrupts masked", sr & 0xA700 == 0x2700, "SR %04x" % sr)   # T, S, mask; the rest is CCR
    check(".data was copied before the constructors", data_seen == DATA_PROBE, "%08x" % data_seen)
    check("constructors ran before bios_main", con_fb_seen == 0 and serial_seen == 1,
          "con_fb %06x, console_serial %d" % (con_fb_seen, serial_seen))
    check("no destructor ran", dtors == 0, "%d" % dtors)
    for name, add in (("ctor_probe_3", 3), ("ctor_probe_x", 0x58), ("ctor_probe_y", 0x59)):
        this, value = struct.unpack(">II", b.peek(name, 8))
        check("C++ object %s constructed in place" % name,
              this == sym[name] and value == DATA_PROBE + add, "self %06x at %06x, value %08x" % (this, sym[name], value))
    check("the constructor got the object's address", cxx_this == sym["ctor_probe_x"] and cxx_value == DATA_PROBE + 0x58,
          "this %06x, value %08x" % (cxx_this, cxx_value))
    # ctors_cxx.cpp includes bios.h and calls the BIOS's ksnprintf: C linkage from the headers
    check("C++ called the BIOS through bios.h", cxx_text == "bios.h C++", "ksnprintf wrote %r" % cxx_text)
    # the BIOS came up as usual afterwards: POST and banner on serial, the monitor took a command
    check("POST and banner on serial", "RAM ... OK" in b.out and "DUART 38400 8N1" in b.out)
    check("monitor ran `info`", re.search(r"ticks \d+ \(60/s\)", b.out) is not None)
    if fails:
        sys.stdout.write(b.out + b.err)

    # The monitor's `reset` goes through _start again with the first boot's record still in
    # RAM: .bss must be cleared and the constructors run again, once.
    print("== monitor `reset`")
    before = fails
    r = Boot(emu, rom, sym, "reset\\n", 900)
    check("emulator exit status 0, no bus errors", r.status == 0 and ", 0 bus errors" in r.summary, r.summary)
    check("the POST ran twice", r.out.count("RAM ... OK") == 2, "%d times" % r.out.count("RAM ... OK"))
    order, n = r.log()[:2]
    check("constructors ran again, once", order.replace("L", "") == ORDER and n == len(ORDER) + 1, "ran %r, %d calls" % (order, n))
    this, value = struct.unpack(">II", r.peek("ctor_probe_x", 8))
    check("C++ object rebuilt", this == sym["ctor_probe_x"] and value == DATA_PROBE + 0x58, "self %06x, value %08x" % (this, value))
    if fails != before:
        sys.stdout.write(r.out + r.err)

    print("ALL TESTS PASSED" if not fails else "%d FAILED" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
