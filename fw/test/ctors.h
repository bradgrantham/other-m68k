/* Static-constructor test (make -C fw test-ctors): the record the test constructors leave in
 * BIOS RAM. ctors.py reads it from the emulator's memory dump, so the layout is fixed:
 * big-endian, offsets in the comments, 56 bytes. */
#ifndef TEST_CTORS_H
#define TEST_CTORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTOR_DATA_PROBE 0x600DDA7Au     /* initial value of a .data variable the constructors read */

struct ctor_log {
    char order[16];             /*  0: one tag per constructor, in the order they ran */
    uint16_t n;                 /* 16: how many ran */
    uint16_t sr;                /* 18: status register in the plain C constructor */
    uint32_t data_seen;         /* 20: the .data probe as the constructors saw it */
    uint32_t con_fb_seen;       /* 24: con_fb then: 0 = bios_main had not run con_init yet */
    uint32_t cxx_value;         /* 28: what the C++ object 'X' computed for its member */
    uint32_t cxx_this;          /* 32: `this` in that constructor */
    uint16_t console_serial_seen; /* 36: 1 = .data copied, bios_main not finished */
    uint16_t dtors;             /* 38: destructors that ran (the BIOS never exits: stays 0) */
    char cxx_text[16];          /* 40: "bios.h C++", formatted by the BIOS's ksnprintf called from C++ */
};

extern struct ctor_log ctor_log;
void ctor_note(char tag);
uint32_t ctor_data_probe(void);

#ifdef __cplusplus
}
#endif

#endif
