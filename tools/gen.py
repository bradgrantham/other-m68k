#!/usr/bin/env python3
"""Generate headers, linker scripts, Verilog parameters, syscall stubs and docs from spec/board.json.

Usage: tools/gen.py [--spec spec/board.json] [--out generated]
Stdlib only. Output is deterministic; files are rewritten only if their content changed.
"""
import argparse, json, os, sys, collections

HDR = "Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`."

def hx(v, width=6):
    return "0x%0*X" % (width, int(v, 0) if isinstance(v, str) else v)

def n(v):
    return int(v, 0) if isinstance(v, str) else int(v)

def cident(s):
    return "".join(c if c.isalnum() else "_" for c in s).upper()

def write(path, text):
    old = None
    if os.path.exists(path):
        with open(path) as f:
            old = f.read()
    if old != text:
        with open(path, "w") as f:
            f.write(text)
        return True
    return False

def field_items(reg):
    for f in reg.get("fields", []):
        w = f.get("width", 1)
        mask = ((1 << w) - 1) << f["bit"]
        yield f, w, mask

def is_pointer(ctype):
    return "*" in ctype

# ---------------------------------------------------------------- C header
def gen_hw_h(d):
    o = []
    A = o.append
    A("/* %s */" % HDR)
    A("#ifndef HW_H\n#define HW_H\n#include <stdint.h>\n")
    b = d["board"]
    A('#define BOARD_NAME "%s"' % b["name"])
    A("#define BOARD_REVISION %d" % b["revision"])
    A("#define OSC_HZ %dUL" % b["osc_hz"])
    A("#define CPU_CLOCK_HZ %dUL" % b["cpu_clock_hz"])
    A("#define SLOT_CPU_CLOCKS %d" % b["slot_cpu_clocks"])
    A("\n/* ---- memory regions ---- */")
    for r in d["memory_map"]:
        nm = cident(r["name"])
        A("#define %s_BASE %sUL" % (nm, hx(r["base"])))
        A("#define %s_SIZE %sUL" % (nm, hx(r["size"])))
        A("#define %s_END  (%s_BASE + %s_SIZE)" % (nm, nm, nm))
    A("\n/* ---- RAM layout ---- */")
    for r in d["ram_layout"]:
        nm = cident(r["name"])
        A("#define %s_BASE %sUL" % (nm, hx(r["base"])))
        A("#define %s_SIZE %sUL" % (nm, hx(r["size"])))
        A("#define %s_END  (%s_BASE + %s_SIZE)" % (nm, nm, nm))
    A("#define SYS_STACK_TOP SYSDATA_END")
    A("#define USER_STACK_TOP USER_END")
    v = d["video"]
    A("\n/* ---- video ---- */")
    A("#define VIDEO_WIDTH %d" % v["width"])
    A("#define VIDEO_HEIGHT %d" % v["height"])
    A("#define VIDEO_BPP %d" % v["bpp"])
    A("#define VIDEO_BYTES_PER_LINE %d" % v["bytes_per_line"])
    A("#define VIDEO_PAGE_COUNT %d" % len(v["pages"]))
    for i, p in enumerate(v["pages"]):
        A("#define FB_PAGE%d_ADDR %sUL" % (i, hx(p)))
    A("#define FB_SIZE (VIDEO_BYTES_PER_LINE * VIDEO_HEIGHT)")
    A("#define RGB332(r, g, b) ((uint8_t)((((r) >> 5) << 5) | (((g) >> 5) << 2) | ((b) >> 6)))")
    for std, t in v["timing"].items():
        S = std.upper()
        for k in ("line_clocks", "h_active", "h_front", "h_sync", "h_back", "lines", "v_active", "v_bottom", "v_sync", "v_top"):
            A("#define VIDEO_%s_%s %d" % (S, k.upper(), t[k]))
        A("#define VIDEO_%s_FRAME_CLOCKS (%dUL * %dUL)" % (S, t["line_clocks"], t["lines"]))
    A("\n/* ---- registers ---- */")
    for blk_name, blk in d["registers"].items():
        prefix = "IO" if blk_name == "io" else "SW"
        A("#define %s_BASE %sUL" % (prefix, hx(blk["base"])))
        for r in blk["regs"]:
            rn = cident(r["name"])
            A("#define %s_%s_OFFSET %d" % (prefix, rn, r["offset"]))
            A("#define %s_%s_ADDR (%s_BASE + %d)" % (prefix, rn, prefix, r["offset"]))
            A("#define %s_%s (*(volatile uint16_t *)%s_%s_ADDR)" % (prefix, rn, prefix, rn))
            if "reset" in r:
                A("#define %s_%s_RESET %s" % (prefix, rn, hx(r["reset"], 4)))
            for f, w, mask in field_items(r):
                fn = "%s_%s" % (rn, cident(f["name"]))
                A("#define %s_BIT %d" % (fn, f["bit"]))
                if w == 1:
                    A("#define %s (1u << %d)" % (fn, f["bit"]))
                else:
                    A("#define %s_SHIFT %d" % (fn, f["bit"]))
                    A("#define %s_WIDTH %d" % (fn, w))
                    A("#define %s_MASK %s" % (fn, hx(mask, 4)))
        if blk_name == "switches":
            A("/* soft switches: writing any value performs the action */")
            for r in blk["regs"]:
                rn = cident(r["name"])
                A("#define SW_%s_SET() do { SW_%s = 0; } while (0)" % (rn, rn))
    A("\n/* ---- interrupts (autovectored) ---- */")
    for i in d["interrupts"]:
        nm = cident(i["name"])
        A("#define IRQ_%s_LEVEL %d" % (nm, i["level"]))
        A("#define IRQ_%s_VECTOR %d" % (nm, 24 + i["level"]))
        A("#define IRQ_%s_VECTOR_ADDR %s" % (nm, hx((24 + i["level"]) * 4, 3)))
    A("\n/* ---- syscall ABI ---- */")
    A("#define SYSCALL_TRAP %d" % d["syscall"]["trap"])
    A("#define SYSCALL_VECTOR %d" % (32 + d["syscall"]["trap"]))
    A("\n/* ---- serial ---- */")
    A("#define SERIAL_BAUD %d" % d["serial"]["baud"])
    A("#define SERIAL_CLOCKS_PER_BIT (CPU_CLOCK_HZ / SERIAL_BAUD)")
    A("\n#endif /* HW_H */")
    return "\n".join(o) + "\n"

# ---------------------------------------------------------------- gas include
def gen_hw_inc(d):
    o = ["/* %s */" % HDR, ""]
    A = o.append
    def eq(k, v): A("\t.equ %s, %s" % (k, v))
    b = d["board"]
    eq("CPU_CLOCK_HZ", b["cpu_clock_hz"])
    for r in d["memory_map"] + d["ram_layout"]:
        nm = cident(r["name"])
        eq(nm + "_BASE", hx(r["base"])); eq(nm + "_SIZE", hx(r["size"]))
        eq(nm + "_END", hx(n(r["base"]) + n(r["size"])))
    eq("SYS_STACK_TOP", hx(n(d["ram_layout"][1]["base"]) + n(d["ram_layout"][1]["size"])))
    for i, p in enumerate(d["video"]["pages"]):
        eq("FB_PAGE%d_ADDR" % i, hx(p))
    for blk_name, blk in d["registers"].items():
        prefix = "IO" if blk_name == "io" else "SW"
        eq(prefix + "_BASE", hx(blk["base"]))
        for r in blk["regs"]:
            rn = cident(r["name"])
            eq("%s_%s_ADDR" % (prefix, rn), hx(n(blk["base"]) + r["offset"]))
            eq("%s_%s_OFFSET" % (prefix, rn), r["offset"])
            for f, w, mask in field_items(r):
                fn = "%s_%s" % (rn, cident(f["name"]))
                eq(fn + "_BIT", f["bit"])
                eq(fn + "_MASK", hx(mask, 4))
    for i in d["interrupts"]:
        nm = cident(i["name"])
        eq("IRQ_%s_LEVEL" % nm, i["level"])
        eq("IRQ_%s_VECTOR_ADDR" % nm, hx((24 + i["level"]) * 4, 3))
    eq("SYSCALL_TRAP", d["syscall"]["trap"])
    eq("SERIAL_CLOCKS_PER_BIT", b["cpu_clock_hz"] // d["serial"]["baud"])
    A("")
    for s in d["syscalls"]:
        eq("SYS_" + s["name"], s["num"])
    return "\n".join(o) + "\n"

# ---------------------------------------------------------------- linker scripts
def gen_memory_ld(d):
    rom = next(r for r in d["memory_map"] if r["name"] == "ROM")
    sysd = next(r for r in d["ram_layout"] if r["name"] == "SYSDATA")
    o = ["/* %s */" % HDR, "MEMORY", "{",
         "    ROM (rx)      : ORIGIN = %s, LENGTH = %s" % (hx(rom["base"]), hx(rom["size"])),
         "    SYSDATA (rwx) : ORIGIN = %s, LENGTH = %s" % (hx(sysd["base"]), hx(sysd["size"])),
         "}",
         "_sys_stack_top = ORIGIN(SYSDATA) + LENGTH(SYSDATA);",
         "_rom_base = ORIGIN(ROM);", "_rom_size = LENGTH(ROM);", ""]
    return "\n".join(o)

def gen_user_ld(d):
    user = next(r for r in d["ram_layout"] if r["name"] == "USER")
    o = ["/* %s */" % HDR, "MEMORY", "{",
         "    USER (rwx) : ORIGIN = %s, LENGTH = %s" % (hx(user["base"]), hx(user["size"])),
         "}",
         "_user_stack_top = ORIGIN(USER) + LENGTH(USER);", ""]
    return "\n".join(o)

# ---------------------------------------------------------------- Verilog
def gen_vh(d):
    o = ["// %s" % HDR, "`ifndef BOARD_PARAMS_VH", "`define BOARD_PARAMS_VH", ""]
    A = o.append
    b = d["board"]
    A("localparam OSC_HZ = %d;" % b["osc_hz"])
    A("localparam CPU_CLOCK_HZ = %d;" % b["cpu_clock_hz"])
    A("localparam SLOT_FAST_CLOCKS = %d;" % b["slot_fast_clocks"])
    A("")
    for r in d["memory_map"]:
        nm = cident(r["name"])
        base, size = n(r["base"]), n(r["size"])
        A("localparam [23:0] %s_BASE = 24'h%06X;" % (nm, base))
        A("localparam [23:0] %s_SIZE = 24'h%06X;" % (nm, size))
        # number of high address bits that must match (A23 down)
        bits = 24 - (size.bit_length() - 1)
        A("localparam %s_DECODE_BITS = %d; // compare A23..A%d against %s_BASE" % (nm, bits, 24 - bits, nm))
    A("")
    v = d["video"]
    for i, p in enumerate(v["pages"]):
        A("localparam [16:0] FB_PAGE%d_WORD = 17'h%05X; // byte address %s >> 1" % (i, n(p) >> 1, hx(p)))
    sc = v["slot_clocks"]
    for std, t in v["timing"].items():
        S = std.upper()
        A("// %s horizontal values in slots (%d CPU clocks each), vertical in lines" % (S, sc))
        A("localparam %s_H_TOTAL  = %d;" % (S, t["line_clocks"] // sc))
        A("localparam %s_H_ACTIVE = %d;" % (S, t["h_active"] // sc))
        A("localparam %s_H_SYNC_START = %d;" % (S, (t["h_active"] + t["h_front"]) // sc))
        A("localparam %s_H_SYNC_END   = %d;" % (S, (t["h_active"] + t["h_front"] + t["h_sync"]) // sc))
        A("localparam %s_V_TOTAL  = %d;" % (S, t["lines"]))
        A("localparam %s_V_ACTIVE = %d;" % (S, t["v_active"]))
        A("localparam %s_V_SYNC_START = %d;" % (S, t["v_active"] + t["v_bottom"]))
        A("localparam %s_V_SYNC_END   = %d;" % (S, t["v_active"] + t["v_bottom"] + t["v_sync"]))
        assert t["line_clocks"] % sc == 0 and t["h_active"] % sc == 0 and t["h_front"] % sc == 0 and t["h_sync"] % sc == 0 and t["h_back"] % sc == 0, "horizontal timing must be slot multiples"
        assert t["h_active"] + t["h_front"] + t["h_sync"] + t["h_back"] == t["line_clocks"]
        assert t["v_active"] + t["v_bottom"] + t["v_sync"] + t["v_top"] == t["lines"]
    A("")
    for blk_name, blk in d["registers"].items():
        prefix = "IO" if blk_name == "io" else "SW"
        for r in blk["regs"]:
            A("localparam [3:0] %s_%s_OFF = 4'd%d; // byte offset %d" % (prefix, cident(r["name"]), r["offset"] // 2, r["offset"]))
            for f, w, mask in field_items(r):
                A("localparam %s_%s_BIT = %d;" % (cident(r["name"]), cident(f["name"]), f["bit"]))
    idreg = next(r for r in d["registers"]["io"]["regs"] if r["name"] == "ID")
    A("localparam [15:0] ID_VALUE = 16'h%04X;" % n(idreg["reset"]))
    A("")
    for i in d["interrupts"]:
        A("localparam [2:0] IRQ_%s_LEVEL = 3'd%d;" % (cident(i["name"]), i["level"]))
    A("localparam SERIAL_FAST_CLOCKS_PER_BIT = %d; // %d / %d" % (round(b["osc_hz"] / d["serial"]["baud"]), b["osc_hz"], d["serial"]["baud"]))
    A("")
    A("`endif")
    return "\n".join(o) + "\n"

# ---------------------------------------------------------------- syscalls
def classify_args(s):
    """Return list of (ctype, name, reg) following the ABI register order."""
    abi = None
    dregs = iter(["d1", "d2", "d3"]); aregs = iter(["a0", "a1"])
    out = []
    for ctype, name in s["args"]:
        reg = next(aregs) if is_pointer(ctype) else next(dregs)
        out.append((ctype, name, reg))
    return out

def gen_syscalls_h(d):
    o = ["/* %s */" % HDR, "#ifndef SYSCALLS_H", "#define SYSCALLS_H", "#include <stdint.h>", ""]
    A = o.append
    A("#define SYSCALL_TRAP %d" % d["syscall"]["trap"])
    A("")
    A("/* ---- ABI constants ---- */")
    for k, v in d["abi"]["constants"].items():
        A("#define %s %d" % (k, v))
    A("")
    A("/* ---- ABI structures ---- */")
    for name, fields in d["abi"]["structs"].items():
        A("struct %s {" % name)
        for ctype, fname in fields:
            A("    %s %s;" % (ctype, fname))
        A("};")
    A("")
    A("/* ---- syscall numbers ---- */")
    A("enum {")
    for s in d["syscalls"]:
        A("    SYS_%s = %d," % (s["name"], s["num"]))
    A("    SYS_COUNT = %d" % (max(s["num"] for s in d["syscalls"]) + 1))
    A("};")
    A("")
    A("/* ---- user-side prototypes (implemented by generated/syscall_stubs.S) ---- */")
    for s in d["syscalls"]:
        args = ", ".join("%s %s" % (t, nme) for t, nme in s["args"]) or "void"
        A("%s sys_%s(%s); /* %s */" % (s["ret"], s["name"], args, s["desc"]))
    A("")
    A("#endif /* SYSCALLS_H */")
    return "\n".join(o) + "\n"

def gen_syscall_stubs(d):
    o = ["/* %s */" % HDR,
         "/* User-side TRAP stubs. m68k-elf gcc passes every argument on the stack, 4 bytes each,",
         "   first argument at 4(sp). Integer args go to d1..d3 and pointer args to a0..a1 in order;",
         "   d2/d3 are callee-saved so they are preserved around the trap. */",
         "\t.text", ""]
    A = o.append
    trap = d["syscall"]["trap"]
    for s in d["syscalls"]:
        args = classify_args(s)
        saves = [r for _, _, r in args if r in ("d2", "d3")]
        A("\t.globl sys_%s" % s["name"])
        A("\t.type sys_%s, @function" % s["name"])
        A("sys_%s:" % s["name"])
        off = 4
        if saves:
            A("\tmovem.l %s,-(%%sp)" % "/".join("%" + r for r in saves))
            off += 4 * len(saves)
        for i, (ctype, name, reg) in enumerate(args):
            A("\tmove.l %d(%%sp),%%%s" % (off + 4 * i, reg))
        if s["num"] < 128:
            A("\tmoveq #%d,%%d0" % s["num"])
        else:
            A("\tmove.l #%d,%%d0" % s["num"])
        A("\ttrap #%d" % trap)
        if saves:
            A("\tmovem.l (%%sp)+,%s" % "/".join("%" + r for r in saves))
        A("\trts")
        A("")
    return "\n".join(o)

def gen_syscall_dispatch(d):
    o = ["/* %s */" % HDR,
         "/* Included by the BIOS trap handler. Expects `struct trap_frame *f` with fields",
         "   d0,d1,d2,d3,a0,a1 (uint32_t) and sets `uint32_t ret`. Each bios_<name>() is",
         "   implemented by the BIOS with the C prototype from generated/syscalls.h. */",
         "switch (f->d0 & 0xFFFF) {"]
    A = o.append
    for s in d["syscalls"]:
        args = classify_args(s)
        call = "bios_%s(%s)" % (s["name"], ", ".join("(%s)f->%s" % (t, r) for t, _, r in args))
        if s["ret"] == "void":
            A("case SYS_%s: %s; ret = 0; break;" % (s["name"], call))
        else:
            A("case SYS_%s: ret = (uint32_t)%s; break;" % (s["name"], call))
    A("default: ret = (uint32_t)E_NOSYS; break;")
    A("}")
    return "\n".join(o) + "\n"

def gen_bios_protos(d):
    o = ["/* %s */" % HDR, "#ifndef BIOS_SYSCALLS_H", "#define BIOS_SYSCALLS_H", "#include \"syscalls.h\"", ""]
    for s in d["syscalls"]:
        args = ", ".join("%s %s" % (t, nme) for t, nme in s["args"]) or "void"
        o.append("%s bios_%s(%s);" % (s["ret"], s["name"], args))
    o += ["", "#endif", ""]
    return "\n".join(o)

# ---------------------------------------------------------------- emulator tables
def gen_emu_map(d):
    o = ["/* %s */" % HDR, "#ifndef EMU_MAP_H", "#define EMU_MAP_H", "#include <stdint.h>", '#include "hw.h"', ""]
    A = o.append
    A("enum emu_region_kind { EMU_RAM, EMU_ROM, EMU_IO, EMU_SWITCHES };")
    A("struct emu_region { const char *name; uint32_t base, size; enum emu_region_kind kind; };")
    A("static const struct emu_region emu_regions[] = {")
    kinds = {"ram": "EMU_RAM", "rom": "EMU_ROM"}
    for r in d["memory_map"]:
        k = kinds.get(r["kind"], "EMU_SWITCHES" if r["name"] == "SWITCHES" else "EMU_IO")
        A('    { "%s", %s, %s, %s },' % (r["name"], hx(r["base"]), hx(r["size"]), k))
    A("};")
    A("#define EMU_REGION_COUNT %d" % len(d["memory_map"]))
    A("")
    for blk_name, blk in d["registers"].items():
        prefix = "IO" if blk_name == "io" else "SW"
        A("static const char *const emu_%s_reg_names[] = {" % blk_name)
        names = {}
        for r in blk["regs"]:
            names[r["offset"] // 2] = r["name"]
        cnt = max(names) + 1
        for i in range(cnt):
            A('    "%s",' % names.get(i, "?"))
        A("};")
        A("#define EMU_%s_REG_COUNT %d" % (prefix, cnt))
    A("static const char *const emu_syscall_names[] = {")
    names = {s["num"]: s["name"] for s in d["syscalls"]}
    for i in range(max(names) + 1):
        A('    "%s",' % names.get(i, "?"))
    A("};")
    A("")
    A("#endif")
    return "\n".join(o) + "\n"

# ---------------------------------------------------------------- docs
def gen_doc(d):
    o = ["<!-- %s -->" % HDR, "# %s hardware reference" % d["board"]["name"], ""]
    A = o.append
    A(d["board"]["desc"]); A("")
    A("CPU clock %.3f MHz from a %.0f MHz oscillator; one slot = %d CPU clocks." % (d["board"]["cpu_clock_hz"] / 1e6, d["board"]["osc_hz"] / 1e6, d["board"]["slot_cpu_clocks"])); A("")
    A("## Memory map"); A("")
    A("| Region | Start | End | Size | Kind | Notes |"); A("|---|---|---|---|---|---|")
    for r in d["memory_map"]:
        A("| %s | %s | %s | %d KB | %s | %s |" % (r["name"], hx(r["base"]), hx(n(r["base"]) + n(r["size"]) - 1), n(r["size"]) // 1024, r["kind"], r["desc"]))
    A(""); A(d["unmapped_policy"]); A("")
    A("## RAM layout"); A("")
    A("| Area | Start | End | Size | Purpose |"); A("|---|---|---|---|---|")
    for r in d["ram_layout"]:
        A("| %s | %s | %s | %d bytes | %s |" % (r["name"], hx(r["base"]), hx(n(r["base"]) + n(r["size"]) - 1), n(r["size"]), r["desc"]))
    A("")
    v = d["video"]
    A("## Video"); A("")
    A("%dx%d, %d bpp %s. %s" % (v["width"], v["height"], v["bpp"], v["format"], v["pixel_desc"])); A("")
    A("Pages: " + ", ".join("page %d at %s" % (i, hx(p)) for i, p in enumerate(v["pages"]))); A("")
    A("Encoder: " + v["encoder"]); A("")
    A("| Standard | Line clocks | H active/front/sync/back | Lines | V active/bottom/sync/top | Rates |"); A("|---|---|---|---|---|---|")
    for std, t in v["timing"].items():
        hz = d["board"]["cpu_clock_hz"] / t["line_clocks"]
        A("| %s | %d | %d/%d/%d/%d | %d | %d/%d/%d/%d | %.3f kHz, %.2f Hz |" % (std.upper(), t["line_clocks"], t["h_active"], t["h_front"], t["h_sync"], t["h_back"], t["lines"], t["v_active"], t["v_bottom"], t["v_sync"], t["v_top"], hz / 1e3, hz / t["lines"]))
    A(""); A(v["timing_note"]); A(""); A(v["line_origin"]); A("")
    for blk_name, blk in d["registers"].items():
        A("## %s registers (base %s, %d-bit)" % (blk_name.upper(), hx(blk["base"]), blk["width"])); A("")
        for r in blk["regs"]:
            A("### %s @ %s (%s)%s" % (r["name"], hx(n(blk["base"]) + r["offset"]), r["access"].upper(), (" reset %s" % hx(r["reset"], 4)) if "reset" in r else ""))
            A(""); A(r["desc"]); A("")
            if r.get("fields"):
                A("| Bit | Name | Description |"); A("|---|---|---|")
                for f, w, mask in field_items(r):
                    bits = "%d" % f["bit"] if w == 1 else "%d..%d" % (f["bit"] + w - 1, f["bit"])
                    A("| %s | %s | %s%s |" % (bits, f["name"], f["desc"], " (read only)" if f.get("ro") else ""))
                A("")
    A("## Interrupts"); A(""); A(d["interrupt_mode"]); A("")
    A("| Level | Name | Vector | Source | Description |"); A("|---|---|---|---|---|")
    for i in d["interrupts"]:
        A("| %d | %s | %d (%s) | %s | %s |" % (i["level"], i["name"], 24 + i["level"], hx((24 + i["level"]) * 4, 3), i["source"], i["desc"]))
    A("")
    A("## Peripherals"); A("")
    for k in ("serial", "ps2", "sd", "audio", "power", "reset"):
        A("**%s**: %s" % (k, "; ".join("%s: %s" % (kk, vv) if not isinstance(vv, list) else "%s: %s" % (kk, ", ".join(vv)) for kk, vv in d[k].items()))); A("")
    A("## System calls"); A(""); A(d["syscall"]["desc"]); A("")
    A("| # | Name | Prototype | Description |"); A("|---|---|---|---|")
    for s in d["syscalls"]:
        args = ", ".join("%s %s" % (t, nme) for t, nme in s["args"]) or "void"
        A("| %d | %s | `%s sys_%s(%s)` | %s |" % (s["num"], s["name"], s["ret"], s["name"], args, s["desc"]))
    A("")
    A("### ABI constants"); A("")
    A("| Name | Value |"); A("|---|---|")
    for k, vv in d["abi"]["constants"].items():
        A("| %s | %d |" % (k, vv))
    A("")
    A("### ABI structures"); A("")
    for name, fields in d["abi"]["structs"].items():
        A("`struct %s { %s }`" % (name, " ".join("%s %s;" % (t, f) for t, f in fields))); A("")
    A("## Glue logic"); A("")
    for c in ("cpld1", "cpld2"):
        g = d["glue"][c]
        A("**%s** (%s): %s" % (c.upper(), g["part"], g["role"])); A("")
        for kind, sigs in g["signals"].items():
            A("* %s: %s" % (kind, ", ".join(sigs)))
        A("")
    A(d["glue"]["bus_buffers"]); A("")
    return "\n".join(o)

# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", default="spec/board.json")
    ap.add_argument("--out", default="generated")
    ap.add_argument("--check", action="store_true", help="exit 1 if any output would change")
    a = ap.parse_args()
    with open(a.spec) as f:
        d = json.load(f, object_pairs_hook=collections.OrderedDict)
    validate(d)
    outputs = {
        "hw.h": gen_hw_h(d),
        "hw.inc": gen_hw_inc(d),
        "memory.ld": gen_memory_ld(d),
        "user.ld": gen_user_ld(d),
        "board_params.vh": gen_vh(d),
        "syscalls.h": gen_syscalls_h(d),
        "syscall_stubs.S": gen_syscall_stubs(d),
        "syscall_dispatch.inc": gen_syscall_dispatch(d),
        "bios_syscalls.h": gen_bios_protos(d),
        "emu_map.h": gen_emu_map(d),
        "HARDWARE.md": gen_doc(d),
    }
    os.makedirs(a.out, exist_ok=True)
    changed = []
    for name, text in outputs.items():
        path = os.path.join(a.out, name)
        if a.check:
            cur = open(path).read() if os.path.exists(path) else None
            if cur != text:
                changed.append(name)
        elif write(path, text):
            changed.append(name)
    if a.check:
        if changed:
            print("generated files out of date:", ", ".join(changed)); sys.exit(1)
        print("generated files up to date")
    else:
        print("wrote %d files (%d changed)" % (len(outputs), len(changed)))

def validate(d):
    # regions must not overlap, sizes powers of two, bases aligned
    regs = sorted(((n(r["base"]), n(r["size"]), r["name"]) for r in d["memory_map"]))
    for (b0, s0, n0), (b1, s1, n1) in zip(regs, regs[1:]):
        assert b0 + s0 <= b1, "regions %s and %s overlap" % (n0, n1)
    for b, s, nm in regs:
        assert s & (s - 1) == 0, "region %s size not power of two" % nm
        assert b % s == 0, "region %s base not aligned to its size" % nm
    ram = next(r for r in d["memory_map"] if r["name"] == "RAM")
    lay = sorted((n(r["base"]), n(r["size"]), r["name"]) for r in d["ram_layout"])
    assert lay[0][0] == n(ram["base"]) and lay[-1][0] + lay[-1][1] == n(ram["base"]) + n(ram["size"]), "ram_layout must tile RAM"
    for (b0, s0, n0), (b1, s1, n1) in zip(lay, lay[1:]):
        assert b0 + s0 == b1, "ram_layout gap/overlap between %s and %s" % (n0, n1)
    v = d["video"]
    fb = v["bytes_per_line"] * v["height"]
    for p in v["pages"]:
        assert any(n(r["base"]) == n(p) and n(r["size"]) == fb for r in d["ram_layout"]), "framebuffer page %s not in ram_layout" % p
    nums = [s["num"] for s in d["syscalls"]]
    assert len(nums) == len(set(nums)), "duplicate syscall numbers"
    for s in d["syscalls"]:
        ints = sum(1 for t, _ in s["args"] if not is_pointer(t)); ptrs = len(s["args"]) - ints
        assert ints <= 3 and ptrs <= 2, "syscall %s exceeds register budget" % s["name"]
    for blk in d["registers"].values():
        offs = [r["offset"] for r in blk["regs"]]
        assert len(offs) == len(set(offs)) and all(o % 2 == 0 for o in offs)
        for r in blk["regs"]:
            used = 0
            for f, w, mask in field_items(r):
                assert used & mask == 0, "overlapping fields in %s" % r["name"]
                used |= mask

if __name__ == "__main__":
    main()
