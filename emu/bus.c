/* Address decode, RAM/ROM, overlay, bus error -- the CPLD1 side of the glue.
 *
 * Regions (generated/hw.h): RAM 0x000000 (1 MB, overlaid by ROM at reset),
 * ROM 0xE00000 (1 MB window: two SST39SF040 flash chips, see "flash" below),
 * IO 0xF00000 (CPLD2 registers), SWITCHES 0xF10000
 * (CPLD1 soft switches), DUART 0xF20000 (68681 on D0..D7, odd addresses,
 * A1..A4 = RS1..RS4, mirrored).  Anything else asserts BERR, except the
 * interrupt acknowledge pattern A23..A4 = 1 (0xFFFFF0..0xFFFFFF) which gets
 * VPA.
 *
 * All CPU accesses funnel through bus_read16/bus_write16 (the board is 16-bit
 * wide); byte accesses are lane-masked halves and long accesses two words,
 * high word first, like the real CPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "hw.h"
#include "emu_map.h"
#include "m68k.h"
#include "emu.h"
#include "bus.h"
#include "io.h"
#include "duart.h"

uint8_t bus_ram[RAM_SIZE];
uint8_t bus_rom[ROM_SIZE];
int bus_overlay = 1;
int bus_rom_modified;
unsigned long bus_berr_count;

/* ---- flash ------------------------------------------------------------
 * The ROM window holds two SST39SF040 (U4 = even bytes, U5 = odd bytes), /CE
 * grounded, /OE on reads, /WE on any write in the real ROM region (writes
 * under the overlay never reach them).  Each chip sees byte address
 * N = (addr - ROM_BASE) / 2 and the byte on its own lane, so the JEDEC
 * sequences are modelled per lane: unlock (AA@5555, 55@2AAA), byte program
 * (A0, data: bits can only be cleared), sector erase (80, AA, 55, 30@sector:
 * 4 KB per chip = FLASH_SECTOR_BYTES in the CPU view), chip erase (.. 10),
 * software ID (90: byte 0 = FLASH_MFR_ID, byte 1 = FLASH_DEV_ID), F0 = exit
 * ID mode / reset.  A write that is not part of a sequence is ignored, as on
 * the chip.  After a program or erase the lane answers a few reads with the
 * status bits (DQ7 inverted, DQ6 toggling) so the firmware's polling loop is
 * exercised, then the array data appears. */
enum { FL_IDLE, FL_AA, FL_UNLOCKED, FL_PROGRAM, FL_ER80, FL_ERAA, FL_ERUNLOCKED };

struct flash_lane {
    int state;
    int id_mode;
    int busy_reads;         /* status reads left before the data shows */
};
static struct flash_lane flash[2];      /* [0] = even/upper lane, [1] = odd/lower lane */

#define FLASH_BUSY_READS_PROGRAM 3
#define FLASH_BUSY_READS_ERASE   6

static void flash_write_byte(int lane, uint32_t n, uint8_t val)
{
    struct flash_lane *f = &flash[lane];
    uint8_t *cell = &bus_rom[2 * n + lane];
    uint32_t chip_n = n & (FLASH_CHIP_SIZE - 1);

    if (f->state == FL_PROGRAM) {
        *cell &= val;
        bus_rom_modified = 1;
        f->busy_reads = FLASH_BUSY_READS_PROGRAM;
        f->state = FL_IDLE;
        return;
    }
    if (f->state == FL_ERUNLOCKED) {
        if (val == 0x30) {
            uint32_t first = n & ~(uint32_t)(FLASH_SECTOR_BYTES / 2 - 1);
            for (uint32_t i = 0; i < FLASH_SECTOR_BYTES / 2; i++)
                bus_rom[2 * (first + i) + lane] = 0xFF;
            bus_rom_modified = 1;
            f->busy_reads = FLASH_BUSY_READS_ERASE;
        } else if (val == 0x10) {
            for (uint32_t i = 0; i < ROM_SIZE / 2; i++)
                bus_rom[2 * i + lane] = 0xFF;
            bus_rom_modified = 1;
            f->busy_reads = FLASH_BUSY_READS_ERASE;
        }
        f->state = FL_IDLE;
        return;
    }
    if (val == 0xF0) {                  /* reset / exit ID mode, any address */
        f->id_mode = 0;
        f->state = FL_IDLE;
        return;
    }
    int at1 = chip_n == 0x5555, at2 = chip_n == 0x2AAA;
    switch (f->state) {
    case FL_IDLE:       f->state = (at1 && val == 0xAA) ? FL_AA : FL_IDLE; break;
    case FL_AA:         f->state = (at2 && val == 0x55) ? FL_UNLOCKED : FL_IDLE; break;
    case FL_UNLOCKED:
        if (at1 && val == 0xA0)      f->state = FL_PROGRAM;
        else if (at1 && val == 0x80) f->state = FL_ER80;
        else if (at1 && val == 0x90) { f->id_mode = 1; f->state = FL_IDLE; }
        else f->state = FL_IDLE;
        break;
    case FL_ER80:       f->state = (at1 && val == 0xAA) ? FL_ERAA : FL_IDLE; break;
    case FL_ERAA:       f->state = (at2 && val == 0x55) ? FL_ERUNLOCKED : FL_IDLE; break;
    default:            f->state = FL_IDLE; break;
    }
}

static uint8_t flash_read_byte(int lane, uint32_t n)
{
    struct flash_lane *f = &flash[lane];
    uint8_t data = bus_rom[2 * n + lane];
    if (f->busy_reads > 0) {
        f->busy_reads--;
        /* DQ7 = complement of the data, DQ6 toggles on each read, rest undefined (0) */
        return (uint8_t)(((~data) & 0x80) | ((f->busy_reads & 1) << 6));
    }
    if (f->id_mode) {
        uint32_t chip_n = n & (FLASH_CHIP_SIZE - 1);
        if (chip_n == 0) return FLASH_MFR_ID;
        if (chip_n == 1) return FLASH_DEV_ID;
    }
    return data;
}

static uint16_t flash_read16(uint32_t n)
{
    return (uint16_t)((flash_read_byte(0, n) << 8) | flash_read_byte(1, n));
}

static void flash_write16(uint32_t n, uint16_t val, int lanes)
{
    if (lanes & 2) flash_write_byte(0, n, (uint8_t)(val >> 8));
    if (lanes & 1) flash_write_byte(1, n, (uint8_t)val);
}

int bus_save_rom(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "m68k-emu: cannot write ROM '%s'\n", path);
        return -1;
    }
    size_t n = fwrite(bus_rom, 1, sizeof bus_rom, f);
    fclose(f);
    if (n != sizeof bus_rom) {
        fprintf(stderr, "m68k-emu: short write to ROM '%s'\n", path);
        return -1;
    }
    fprintf(stderr, "m68k-emu: ROM contents (%lu bytes) saved to %s\n", (unsigned long)ROM_SIZE, path);
    return 0;
}

/* Function code of the current access, written by Musashi through the
 * M68K_SET_FC_CALLBACK macro in m68kconf.h. */
unsigned int emu_fc;

/* Musashi internals used to fill the 68000 group-0 bus error frame (see
 * musashi-berr-frame.patch): they are declared in m68kcpu.h which we do not
 * include. */
extern unsigned int m68ki_aerr_address;
extern unsigned int m68ki_aerr_write_mode;
extern unsigned int m68ki_aerr_fc;
#define MUSASHI_MODE_READ  0x10
#define MUSASHI_MODE_WRITE 0

#define IACK_BASE 0xFFFFF0u
#define DOUBLE_FAULT_ADDR 0x00FFFF01u   /* Musashi reads this before HALTing */

/* ---- ROM loading ------------------------------------------------------ */

int bus_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "m68k-emu: cannot open ROM '%s'\n", path);
        return -1;
    }
    memset(bus_rom, 0xFF, sizeof bus_rom);
    size_t n = fread(bus_rom, 1, sizeof bus_rom, f);
    int extra = fgetc(f);
    fclose(f);
    if (extra != EOF) {
        fprintf(stderr, "m68k-emu: ROM '%s' is larger than %lu bytes\n", path,
                (unsigned long)ROM_SIZE);
        return -1;
    }
    if (n == 0) {
        fprintf(stderr, "m68k-emu: ROM '%s' is empty\n", path);
        return -1;
    }
    if (n < sizeof bus_rom)
        fprintf(stderr, "m68k-emu: ROM is %zu bytes, padded to %lu with 0xFF\n", n,
                (unsigned long)ROM_SIZE);
    return 0;
}

void bus_reset(void)
{
    bus_overlay = 1;
    memset(flash, 0, sizeof flash);
}

/* ---- bus error -------------------------------------------------------- */

static uint64_t berr_window_start;
static unsigned berr_window_count;
static unsigned berr_suppressed;

static void berr_log(uint32_t addr, int write)
{
    uint64_t now = emu_cycles();
    if (now - berr_window_start >= CPU_CLOCK_HZ) {
        if (berr_suppressed)
            fprintf(stderr, "m68k-emu: (%u further bus errors not shown)\n", berr_suppressed);
        berr_window_start = now;
        berr_window_count = 0;
        berr_suppressed = 0;
    }
    if (berr_window_count < 10) {
        berr_window_count++;
        fprintf(stderr, "m68k-emu: bus error: %s %06X at PC=%06X (FC=%u, cycle %llu)\n",
                write ? "write" : "read", addr, m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF,
                emu_fc & 7, (unsigned long long)now);
    } else {
        berr_suppressed++;
    }
}

static void bus_error(uint32_t addr, int write)
{
    bus_berr_count++;
    berr_log(addr, write);
    if (opts.exit_on_berr)
        emu_request_exit(2);
    m68ki_aerr_address = addr;
    m68ki_aerr_write_mode = write ? MUSASHI_MODE_WRITE : MUSASHI_MODE_READ;
    m68ki_aerr_fc = emu_fc & 7;
    m68k_pulse_bus_error();     /* longjmps back into m68k_execute(); never returns */
}

/* ---- decode ----------------------------------------------------------- */

static inline int in_region(uint32_t addr, uint32_t base, uint32_t size)
{
    return addr >= base && addr < base + size;
}

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* ---- DUART ------------------------------------------------------------ */

/* The 68681 sits on D0..D7, i.e. the odd byte lane, with RS4..RS1 = A4..A1.
 * CPLD1 asserts DUARTCS for any access in the region, so word and even-byte
 * accesses complete (DTACK from the chip) but only the odd byte carries data:
 * a word read returns 0xFF in the upper byte, a word write delivers its low
 * byte, an even-byte access reaches no register here (its data bus half is
 * floating; what the chip makes of it on hardware is undefined). */
static int duart_warned_word, duart_warned_even;

static void duart_width_warning(int *flag, const char *what, uint32_t addr, int write)
{
    if (*flag)
        return;
    *flag = 1;
    fprintf(stderr, "m68k-emu: %s %s %06X at PC=%06X: only odd-address byte accesses reach the DUART; "
            "further occurrences not shown\n", what, write ? "write to" : "read of", addr,
            m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF);
}

static uint16_t duart_read16(uint32_t addr, int lanes)
{
    unsigned rs = (addr >> 1) & 0xF;
    if (lanes == 3)
        duart_width_warning(&duart_warned_word, "word", addr, 0);
    else if (!(lanes & 1))
        duart_width_warning(&duart_warned_even, "even-byte", addr, 0);
    if (!(lanes & 1))
        return 0xFFFF;
    return (uint16_t)(0xFF00 | duart_read(rs));
}

static void duart_write16(uint32_t addr, uint16_t val, int lanes)
{
    unsigned rs = (addr >> 1) & 0xF;
    if (lanes == 3)
        duart_width_warning(&duart_warned_word, "word", addr, 1);
    else if (!(lanes & 1))
        duart_width_warning(&duart_warned_even, "even-byte", addr, 1);
    if (lanes & 1)
        duart_write(rs, (uint8_t)val);
}

/* One 16-bit CPU bus cycle.  lanes: bit 1 = upper byte (even address, UDS),
 * bit 0 = lower byte (odd address, LDS). */
static uint16_t bus_read16(uint32_t addr, int lanes)
{
    addr &= 0xFFFFFF;
    uint32_t a = addr & ~1u;
    uint16_t v;

    if (in_region(a, RAM_BASE, RAM_SIZE)) {
        if (bus_overlay)
            v = flash_read16(((a - RAM_BASE) & (ROM_SIZE - 1)) / 2);
        else
            v = rd16(&bus_ram[a - RAM_BASE]);
    } else if (in_region(a, ROM_BASE, ROM_SIZE)) {
        v = flash_read16((a - ROM_BASE) / 2);
        bus_overlay = 0;                /* clears at the end of the cycle */
    } else if (in_region(a, IO_BASE, IO_SIZE)) {
        v = io_read16(a & 0xE, lanes);
    } else if (in_region(a, SWITCHES_BASE, SWITCHES_SIZE)) {
        v = 0xFFFF;                     /* write-only; floating bus */
    } else if (in_region(a, DUART_BASE, DUART_SIZE)) {
        v = duart_read16(addr, lanes);
    } else if (a >= IACK_BASE) {
        if (addr == DOUBLE_FAULT_ADDR)
            emu_cpu_halted();
        v = 0xFFFF;                     /* autovector space: VPA, no data */
    } else {
        bus_error(addr, 0);
        v = 0xFFFF;                     /* not reached */
    }
    return v;
}

static void bus_write16(uint32_t addr, uint16_t val, int lanes)
{
    addr &= 0xFFFFFF;
    uint32_t a = addr & ~1u;

    if (in_region(a, RAM_BASE, RAM_SIZE)) {
        if (bus_overlay)
            return;                     /* overlaid ROM: DTACK, ignored */
        uint8_t *p = &bus_ram[a - RAM_BASE];
        if (lanes & 2) p[0] = (uint8_t)(val >> 8);
        if (lanes & 1) p[1] = (uint8_t)val;
    } else if (in_region(a, ROM_BASE, ROM_SIZE)) {
        flash_write16((a - ROM_BASE) / 2, val, lanes);   /* ROMWE: DTACK, chips see it */
        bus_overlay = 0;                /* overlay still clears */
    } else if (in_region(a, IO_BASE, IO_SIZE)) {
        io_write16(a & 0xE, val, lanes);
    } else if (in_region(a, SWITCHES_BASE, SWITCHES_SIZE)) {
        io_switch_write(a & 6);
    } else if (in_region(a, DUART_BASE, DUART_SIZE)) {
        duart_write16(addr, val, lanes);
    } else if (a >= IACK_BASE) {
        /* nothing there; VPA, ignored */
    } else {
        bus_error(addr, 1);
    }
}

/* ---- Musashi memory interface ---------------------------------------- */

unsigned int m68k_read_memory_8(unsigned int address)
{
    uint16_t w = bus_read16(address, (address & 1) ? 1 : 2);
    return (address & 1) ? (w & 0xFF) : (w >> 8);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return bus_read16(address, 3);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    uint32_t hi = bus_read16(address, 3);
    uint32_t lo = bus_read16(address + 2, 3);
    return (hi << 16) | lo;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    value &= 0xFF;
    /* The 68000 drives the byte on both halves of the data bus. */
    bus_write16(address, (uint16_t)((value << 8) | value), (address & 1) ? 1 : 2);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    bus_write16(address, (uint16_t)value, 3);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    bus_write16(address, (uint16_t)(value >> 16), 3);
    bus_write16(address + 2, (uint16_t)value, 3);
}

/* ---- side-effect-free peeks ------------------------------------------ */

uint8_t bus_peek8(uint32_t addr)
{
    addr &= 0xFFFFFF;
    if (in_region(addr, RAM_BASE, RAM_SIZE)) {
        if (bus_overlay)
            return bus_rom[(addr - RAM_BASE) & (ROM_SIZE - 1)];
        return bus_ram[addr - RAM_BASE];
    }
    if (in_region(addr, ROM_BASE, ROM_SIZE))
        return bus_rom[addr - ROM_BASE];
    if (in_region(addr, IO_BASE, IO_SIZE)) {
        uint16_t w = io_peek16(addr & 0xE);
        return (addr & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (in_region(addr, DUART_BASE, DUART_SIZE))
        return (addr & 1) ? duart_peek((addr >> 1) & 0xF) : 0xFF;
    return 0xFF;
}

uint16_t bus_peek16(uint32_t addr)
{
    return (uint16_t)((bus_peek8(addr) << 8) | bus_peek8(addr + 1));
}

uint32_t bus_peek32(uint32_t addr)
{
    return ((uint32_t)bus_peek16(addr) << 16) | bus_peek16(addr + 2);
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return bus_peek8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return bus_peek16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return bus_peek32(address);
}

void bus_dump(uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;
    for (uint32_t line = addr & ~15u; line < end; line += 16) {
        fprintf(stderr, "%06X:", line & 0xFFFFFF);
        char ascii[17];
        for (int i = 0; i < 16; i++) {
            uint32_t a = line + i;
            if (a < addr || a >= end) {
                fprintf(stderr, "   ");
                ascii[i] = ' ';
            } else {
                uint8_t b = bus_peek8(a);
                fprintf(stderr, " %02X", b);
                ascii[i] = isprint(b) ? (char)b : '.';
            }
        }
        ascii[16] = 0;
        fprintf(stderr, "  |%s|\n", ascii);
    }
}
