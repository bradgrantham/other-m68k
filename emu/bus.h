/* Address decode, RAM/ROM, overlay, bus error -- the CPLD1 side of the glue. */
#ifndef BUS_H
#define BUS_H

#include <stdint.h>

extern uint8_t bus_ram[];       /* RAM_SIZE bytes, big-endian as the CPU sees it */
extern uint8_t bus_rom[];       /* ROM_SIZE bytes */
extern int bus_overlay;         /* ROM overlaid on the RAM region (set at reset) */

/* Load a ROM image (padded with 0xFF to ROM_SIZE).  Returns 0 or -1. */
int bus_load_rom(const char *path);

/* Power-on / reset-button state: overlay set, BERR statistics kept. */
void bus_reset(void);

/* Side-effect-free peeks for the disassembler, --dump-mem and the video
 * renderer.  Unmapped addresses read as 0xFF, I/O reads shadow registers. */
uint8_t  bus_peek8(uint32_t addr);
uint16_t bus_peek16(uint32_t addr);
uint32_t bus_peek32(uint32_t addr);

/* Hex dump of len bytes starting at addr to stderr (16 bytes per line). */
void bus_dump(uint32_t addr, uint32_t len);

/* Statistics for the final report. */
extern unsigned long bus_berr_count;

#endif
