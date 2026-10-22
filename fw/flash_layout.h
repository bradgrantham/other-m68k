/* struct flash_op offsets shared by flash.c and flashram.S (assembler-safe: macros only). */
#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#define FO_CMD1         0       /* uint32_t  FLASH_CMD1 */
#define FO_CMD2         4       /* uint32_t  FLASH_CMD2 */
#define FO_DST          8       /* uint32_t  sector base (op 1) or ROM_BASE (op 0) */
#define FO_SRC          12      /* uint32_t  const uint16_t *: words to program */
#define FO_NWORDS       16      /* uint32_t */
#define FO_ERASE_LOOPS  20      /* uint32_t  poll iterations before giving up */
#define FO_PROG_LOOPS   24      /* uint32_t */
#define FO_OP           28      /* uint16_t  0 = ID, 1 = image update (reboots), 2 = one sector, returns */
#define FO_ID_MFR       30      /* uint16_t  out: word at ROM_BASE in ID mode */
#define FO_ID_DEV       32      /* uint16_t  out: word at ROM_BASE + 2 */
#define FO_SECTOR_WORDS 34      /* uint16_t  words per flash sector (CPU view), ops 1 and 2 */
#define FO_VBLANKS      36      /* uint32_t  out (op 2): VBLANK flags seen and cleared while interrupts were masked */
#define FO_SIZE         40


#endif
