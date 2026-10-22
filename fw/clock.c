/* BIOS wall clock and calendar arithmetic (see clock.h). Date algorithms are the proleptic
 * Gregorian days-from-civil / civil-from-days (H. Hinnant), valid for the whole 32-bit range
 * (1970..2106). FAT stamps: date = (year-1980) << 9 | month << 5 | day, time = hour << 11 |
 * minute << 5 | second/2. */
#include "bios.h"
#include "clock.h"
#include "ff.h"                        /* DWORD, for get_fattime */

uint32_t clock_frame_clocks = VIDEO_NTSC_LINE_CLOCKS * VIDEO_NTSC_LINES;   /* main.c sets PAL */
static uint32_t clock_secs;                    /* 0 = not set */
static uint32_t clock_acc;                     /* CPU clocks towards the next second */

void clock_tick(void)
{
    if (!clock_secs) return;
    clock_acc += clock_frame_clocks;
    if (clock_acc >= CPU_CLOCK_HZ) { clock_acc -= CPU_CLOCK_HZ; clock_secs++; }
}

void clock_add_frames(uint32_t n)
{
    while (n--) clock_tick();
}

uint32_t clock_get(void) { return clock_secs; }
void clock_set(uint32_t secs) { clock_acc = 0; clock_secs = secs; }

static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int32_t)era * 146097 + doe - 719468;
}

static void civil_from_days(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yoe + era * 400) + (*m <= 2);
}

void clock_to_tm(uint32_t secs, struct clock_tm *t)
{
    uint32_t days = secs / 86400, rem = secs % 86400;
    civil_from_days((int32_t)days, &t->year, &t->mon, &t->day);
    t->hour = (int)(rem / 3600);
    t->min = (int)(rem % 3600 / 60);
    t->sec = (int)(rem % 60);
}

uint32_t clock_from_tm(const struct clock_tm *t)
{
    return (uint32_t)days_from_civil(t->year, t->mon, t->day) * 86400u +
           (uint32_t)t->hour * 3600u + (uint32_t)t->min * 60u + (uint32_t)t->sec;
}

uint32_t clock_from_fat(unsigned fdate, unsigned ftime)
{
    if (fdate == 0) return 0;
    struct clock_tm t;
    t.year = 1980 + (int)(fdate >> 9);
    t.mon = (int)((fdate >> 5) & 15);
    t.day = (int)(fdate & 31);
    t.hour = (int)(ftime >> 11);
    t.min = (int)((ftime >> 5) & 63);
    t.sec = (int)(ftime & 31) * 2;
    if (t.mon < 1 || t.mon > 12 || t.day < 1) return 0;
    return clock_from_tm(&t);
}

uint32_t clock_to_fat(uint32_t secs)
{
    struct clock_tm t;
    clock_to_tm(secs, &t);
    if (t.year < 1980) t.year = 1980;
    if (t.year > 2107) t.year = 2107;
    return ((uint32_t)(t.year - 1980) << 25) | ((uint32_t)t.mon << 21) | ((uint32_t)t.day << 16) |
           ((uint32_t)t.hour << 11) | ((uint32_t)t.min << 5) | ((uint32_t)t.sec >> 1);
}

static int num(const char **s, int digits, int lo, int hi, int *out)
{
    int v = 0;
    for (int i = 0; i < digits; i++) {
        if (**s < '0' || **s > '9') return 0;
        v = v * 10 + (*(*s)++ - '0');
    }
    if (v < lo || v > hi) return 0;
    *out = v;
    return 1;
}

int clock_parse(const char *s, uint32_t *secs)
{
    struct clock_tm t = { 0, 0, 0, 0, 0, 0 };
    static const int mdays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (!num(&s, 4, 1970, 2105, &t.year) || *s++ != '-' || !num(&s, 2, 1, 12, &t.mon) || *s++ != '-' ||
        !num(&s, 2, 1, mdays[t.mon - 1], &t.day)) return 0;
    if (t.mon == 2 && t.day == 29 && !(t.year % 4 == 0 && (t.year % 100 != 0 || t.year % 400 == 0))) return 0;
    while (*s == ' ') s++;
    if (*s) {
        if (!num(&s, 2, 0, 23, &t.hour) || *s++ != ':' || !num(&s, 2, 0, 59, &t.min)) return 0;
        if (*s == ':') { s++; if (!num(&s, 2, 0, 59, &t.sec)) return 0; }
        while (*s == ' ') s++;
        if (*s) return 0;
    }
    *secs = clock_from_tm(&t);
    return 1;
}

void clock_format(uint32_t secs, char *buf)
{
    struct clock_tm t;
    clock_to_tm(secs, &t);
    ksnprintf(buf, 20, "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.mon, t.day, t.hour, t.min, t.sec);
}

/* FatFs: timestamp for files it writes. A fixed date when the clock has not been set. */
DWORD get_fattime(void)
{
    uint32_t s = clock_secs;
    if (!s) return ((2026u - 1980u) << 25) | (1u << 21) | (1u << 16);
    return clock_to_fat(s);
}
