/* Shared host test helpers. */
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bios.h"

extern int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { fails++; printf("FAIL %s:%d: %s = %ld, expected %s = %ld\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b))) { fails++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

void test_sd(void);
void test_elf(const char *hello_elf);
void test_fs(const char *image, const char *flash_image);
void test_clock(void);
void test_audio(void);
/* test_main.c: DUART channel A stand-in used by fs.c's /dev/ttya */
extern char test_serial_out[256]; extern int test_serial_out_n;
void test_serial_feed(const char *s);
void test_serial_reset(void);

#endif
