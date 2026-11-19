/* Static-constructor test, C side. Linked only into build/test/rom_ctors.elf (make
 * test-ctors); the BIOS itself has none of this. Every constructor appends its tag to
 * ctor_log.order, which must read P123cXY: .preinit_array, then priorities 101, 200 and 300,
 * then the plain ones in link order (this file, then ctors_cxx.cpp). L, the entry in an
 * old-style .ctors section, runs somewhere among the plain ones. */
#include "bios.h"
#include "ctors.h"

uint16_t irq_disable(void);
void irq_restore(uint16_t sr);
extern int console_serial;

struct ctor_log ctor_log;                               /* .bss: crt0 cleared it before the constructors */
static volatile uint32_t data_probe = CTOR_DATA_PROBE;  /* .data: crt0 copied it from the ROM */

void ctor_note(char tag)
{
    if (ctor_log.n < sizeof ctor_log.order) ctor_log.order[ctor_log.n] = tag;
    ctor_log.n++;
}

uint32_t ctor_data_probe(void) { return data_probe; }

static void pre(void) { ctor_note('P'); }
__attribute__((section(".preinit_array"), used)) static void (*pre_entry)(void) = pre;

__attribute__((constructor(101))) static void first(void) { ctor_note('1'); }

__attribute__((constructor)) static void plain(void)
{
    uint16_t sr = irq_disable();
    irq_restore(sr);
    ctor_log.sr = sr;
    ctor_log.data_seen = data_probe;
    ctor_log.con_fb_seen = (uint32_t)con_fb;
    ctor_log.console_serial_seen = (uint16_t)console_serial;
    ctor_note('c');
}

static void legacy(void) { ctor_note('L'); }
__attribute__((section(".ctors"), used)) static void (*legacy_entry)(void) = legacy;

__attribute__((destructor)) static void never(void) { ctor_log.dtors++; }
