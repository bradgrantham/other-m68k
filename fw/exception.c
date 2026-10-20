/* Exception reporting. crt0.S fills `exc` and calls exception_entry() on a fresh stack. */
#include "bios.h"

struct exc_frame exc;

static const char *const names_low[] = {
    "Reset SSP", "Reset PC", "Bus error", "Address error", "Illegal instruction",
    "Zero divide", "CHK", "TRAPV", "Privilege violation", "Trace", "Line 1010 emulator",
    "Line 1111 emulator", "Reserved", "Coprocessor violation", "Format error",
    "Uninitialized interrupt",
};

const char *exception_name(int v)
{
    if (v >= 0 && v < 16) return names_low[v];
    if (v < 24) return "Reserved";
    if (v == 24) return "Spurious interrupt";
    if (v == 31) return "Level 7 autovector (NMI / break)";
    if (v < 32) return "Autovector interrupt";
    if (v < 48) return "TRAP";
    if (v < 64) return "Reserved";
    return "User vector";
}

void exception_print_regs(void)
{
    if (!exc.valid) { kprintf("no exception recorded\n"); return; }
    kprintf("PC=%08x SR=%04x", exc.pc, exc.sr);
    if (exc.group0)
        kprintf(" fault %08x %s FC=%d IR=%04x", exc.fault_addr,
                (exc.fc & 0x10) ? "read" : "write", exc.fc & 7, exc.ir);
    kprintf("\n");
    for (int i = 0; i < 8; i++)
        kprintf("D%d=%08x%s", i, exc.d[i], (i & 3) == 3 ? "\n" : " ");
    for (int i = 0; i < 7; i++)
        kprintf("A%d=%08x%s", i, exc.a[i], (i & 3) == 3 ? "\n" : " ");
    kprintf("A7=%08x\n", (exc.sr & 0x2000) ? exc.ssp : exc.usp);
    kprintf("SSP=%08x USP=%08x\n", exc.ssp, exc.usp);
}

void exception_entry(void)
{
    exc.valid = 1;
    user_cleanup();
    int v = exc.vector;
    kprintf("\n*** Exception %d: %s", v, exception_name(v));
    if (v >= 32 && v < 48) kprintf(" #%d", v - 32);
    if (v >= 25 && v < 32) kprintf(" (level %d)", v - 24);
    kprintf("\n");
    exception_print_regs();
    /* back to the monitor (crt0.S: monitor_restart) */
}
