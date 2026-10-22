/* BIOS wall clock: seconds since 1970-01-01 00:00 (local time, no zone), advanced from the
 * VBLANK interrupt by the exact frame length; 0 until set (spec "filesystem" clock). Also
 * the calendar conversions FatFs and the monitor's `date` command need. Pure C apart from
 * the two globals; host tested. */
#ifndef CLOCK_H
#define CLOCK_H
#include <stdint.h>

struct clock_tm { int year, mon, day, hour, min, sec; };   /* 1970.., 1..12, 1..31, 0..23, 0..59, 0..59 */

extern uint32_t clock_frame_clocks;     /* CPU clocks per video frame (set with the video standard) */

void     clock_tick(void);              /* one frame has passed (VBLANK ISR) */
void     clock_add_frames(uint32_t n);  /* n frames passed while interrupts were masked */
uint32_t clock_get(void);               /* 0 = never set */
void     clock_set(uint32_t secs);

void     clock_to_tm(uint32_t secs, struct clock_tm *t);
uint32_t clock_from_tm(const struct clock_tm *t);
uint32_t clock_from_fat(unsigned fdate, unsigned ftime);   /* FAT directory stamp -> seconds; 0 for a zero date */
uint32_t clock_to_fat(uint32_t secs);                      /* (fdate << 16) | ftime */
/* "YYYY-MM-DD[ HH:MM[:SS]]" -> seconds; 1 ok, 0 malformed */
int      clock_parse(const char *s, uint32_t *secs);
/* "YYYY-MM-DD HH:MM:SS" into buf[20] */
void     clock_format(uint32_t secs, char *buf);

#endif
