/* In-system reprogramming of the ROM: two SST39SF040 (U4 even bytes, U5 odd bytes),
 * /CE tied low, /OE = ROMOE, /WE = ROMWE from CPLD1 on any write in the ROM region.
 * Addresses and chip facts come from generated/hw.h (spec "flash").
 *
 * Image update: the image is read from a file into user RAM (USER_BASE.., ROM_IMAGE_SIZE
 * bytes, below both framebuffer pages), checked (size and the mkrom.py word sum), then
 * handed to flash_ram_op in flashram.S, which lives in .data (BIOS RAM) and, with
 * interrupts masked, erases/programs/verifies the flash one FLASH_SECTOR_BYTES sector at
 * a time and jumps through the new image's reset vectors.  Nothing may run from the
 * flash once the first sector is erased (this ROM is being replaced under its own feet),
 * hence no C code after the hand-over.
 *
 * Sector write (flash_write_sector): the same routine's op 2, for the C: volume; it
 * returns, and reports how many VBLANKs passed while interrupts were off. */
#include "bios.h"
#include "fs.h"
#include "flash.h"
#include "clock.h"

extern int flash_ram_op(struct flash_op *op);        /* flashram.S, in .data */

#define IMAGE_BUF   ((uint8_t *)USER_BASE)

_Static_assert(USER_BASE + ROM_IMAGE_SIZE <= USER_END, "image staging area leaves the user region (framebuffers)");
_Static_assert(ROM_IMAGE_SIZE % FLASH_SECTOR_BYTES == 0, "image is not a whole number of sectors");
_Static_assert(FLASH_VOL_OFFSET >= ROM_IMAGE_SIZE && FLASH_VOL_OFFSET % FLASH_SECTOR_BYTES == 0, "volume overlaps the image or a sector");
_Static_assert(sizeof(struct flash_op) == FO_SIZE, "flash_op layout");

/* Poll loop iterations: one iteration is ~40 clocks (3.3 us at 12 MHz); allow 30x the
 * data-sheet maximum for erase and about 10x for a byte program. */
#define ERASE_LOOPS ((uint32_t)FLASH_T_SECTOR_ERASE_MS * 10000UL)
#define PROG_LOOPS  ((uint32_t)FLASH_T_BYTE_PROGRAM_US * 3UL + 32UL)

static int run_op(struct flash_op *op)
{
    uint16_t sr = irq_disable();
    int rc = flash_ram_op(op);
    if (op->vblanks) {                      /* frames the masked VBLANK interrupt missed */
        ticks += op->vblanks;
        clock_add_frames(op->vblanks);
    }
    irq_restore(sr);
    return rc;
}

static void op_init(struct flash_op *op)
{
    memset(op, 0, sizeof *op);
    op->cmd1 = FLASH_CMD1;
    op->cmd2 = FLASH_CMD2;
    op->erase_loops = ERASE_LOOPS;
    op->prog_loops = PROG_LOOPS;
}

int flash_read_id(uint16_t *mfr, uint16_t *dev)
{
    struct flash_op op;
    op_init(&op);
    op.op = 0;
    op.dst = ROM_BASE;
    run_op(&op);
    *mfr = op.id_mfr;
    *dev = op.id_dev;
    uint16_t want_mfr = (uint16_t)((FLASH_MFR_ID << 8) | FLASH_MFR_ID);
    uint16_t want_dev = (uint16_t)((FLASH_DEV_ID << 8) | FLASH_DEV_ID);
    return op.id_mfr == want_mfr && op.id_dev == want_dev;
}

static int read_image(const char *path)
{
    int fd = fs_open(O_RDONLY, path);
    if (fd < 0) { kprintf("flash: %s: %s\n", path, fs_strerror(fd)); return -1; }
    int size = fs_size(fd);
    if (size != (int)ROM_IMAGE_SIZE) {
        kprintf("flash: %s is %d bytes, a ROM image is %u\n", path, size, (unsigned)ROM_IMAGE_SIZE);
        fs_close(fd);
        return -1;
    }
    uint32_t got = 0;
    while (got < ROM_IMAGE_SIZE) {
        int n = fs_read(fd, IMAGE_BUF + got, FLASH_SECTOR_BYTES);
        if (n <= 0) { kprintf("flash: read error at %u: %s\n", got, n ? fs_strerror(n) : "short file"); fs_close(fd); return -1; }
        got += (uint32_t)n;
        kprintf("\rreading %uK", got / 1024);
    }
    fs_close(fd);
    kprintf("\n");
    uint16_t sum = 0;
    const uint16_t *w = (const uint16_t *)IMAGE_BUF;
    for (uint32_t i = 0; i < ROM_IMAGE_SIZE / 2; i++) sum = (uint16_t)(sum + w[i]);
    if (sum != 0) {
        kprintf("flash: image word sum is %04x, not 0000 (not a mkrom.py image?)\n", sum);
        return -1;
    }
    kprintf("image ok, checksum word %04x\n", w[ROM_IMAGE_SIZE / 2 - 1]);
    return 0;
}

void flash_update(const char *path)
{
    if (read_image(path) < 0) return;
    uint16_t mfr, dev;
    if (!flash_read_id(&mfr, &dev)) {
        kprintf("flash: chip ID %04x/%04x, expected %02x%02x/%02x%02x: not SST39SF040s (EPROMs fitted?)\n",
                mfr, dev, FLASH_MFR_ID, FLASH_MFR_ID, FLASH_DEV_ID, FLASH_DEV_ID);
        return;
    }
    kprintf("Reprogram the ROM (%u sectors of %u bytes) from %s? C: is not touched. Do not press NMI. (y/N) ",
            (unsigned)(ROM_IMAGE_SIZE / FLASH_SECTOR_BYTES), (unsigned)FLASH_SECTOR_BYTES, path);
    int c = input_getchar();
    kprintf("%c\n", (c >= ' ' && c < 127) ? c : ' ');
    if (c != 'y' && c != 'Y') { kprintf("flash: cancelled\n"); return; }

    kprintf("programming %u sectors: one dot per sector on the serial port, then the new ROM boots\n",
            (unsigned)(ROM_IMAGE_SIZE / FLASH_SECTOR_BYTES));
    struct flash_op op;
    op_init(&op);
    op.op = 1;
    op.dst = ROM_BASE;
    op.src = (uint32_t)(uintptr_t)IMAGE_BUF;
    op.nwords = ROM_IMAGE_SIZE / 2;
    op.sector_words = FLASH_SECTOR_BYTES / 2;
    irq_disable();
    flash_ram_op(&op);                  /* never returns: reboots or stops */
    kprintf("flash: unexpected return from the RAM routine\n");
}

int flash_write_sector(void *dst, const uint16_t *src)
{
    struct flash_op op;
    op_init(&op);
    op.op = 2;
    op.dst = (uint32_t)(uintptr_t)dst;
    op.src = (uint32_t)(uintptr_t)src;
    op.nwords = FLASH_SECTOR_BYTES / 2;
    op.sector_words = FLASH_SECTOR_BYTES / 2;
    return run_op(&op);
}
