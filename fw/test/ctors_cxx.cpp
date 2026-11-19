// Static-constructor test, C++ side (see ctors.c). Three static objects whose constructors
// cannot be folded at compile time (they call into C and read a .data variable), one of
// them with an init_priority, and a prioritised constructor function. The destructor has a
// side effect, so each constructor registers it with __cxa_atexit (crt0.S), which the BIOS
// never calls back.
//
// It is also the check that the BIOS headers work from C++: every one of them is included
// here as it stands (they carry their own extern "C"), and the constructor of X calls the
// BIOS's ksnprintf, which only links if bios.h gave it C linkage.
#include "bios.h"
#include "args.h"
#include "audio.h"
#include "clock.h"
#include "duart.h"
#include "elf.h"
#include "exec.h"
#include "flash.h"
#include "flashdisk.h"
#include "flash_layout.h"
#include "fs.h"
#include "joy.h"
#include "lineedit.h"
#include "sd.h"
#include "ctors.h"

struct Probe {
    const Probe *self;
    uint32_t value;
    Probe(char tag, uint32_t add) : self(this), value(ctor_data_probe() + add)
    {
        if (tag == 'X') {
            ctor_log.cxx_value = value;
            ctor_log.cxx_this = reinterpret_cast<uint32_t>(this);
            ksnprintf(ctor_log.cxx_text, sizeof ctor_log.cxx_text, "bios.h %s", "C++");
        }
        ctor_note(tag);
    }
    ~Probe() { ctor_log.dtors++; }
};

__attribute__((constructor(200))) static void second() { ctor_note('2'); }

Probe ctor_probe_3 __attribute__((init_priority(300))) ('3', 3);
Probe ctor_probe_x('X', 0x58);
Probe ctor_probe_y('Y', 0x59);
