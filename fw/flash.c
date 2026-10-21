/* In-system reprogramming of the ROM: two SST39SF040 (U4 even bytes, U5 odd bytes),
 * /CE tied low, /OE = ROMOE, /WE = ROMWE from CPLD1 on any write in the ROM region.
 * Addresses and chip facts come from generated/hw.h (spec "flash").
 *
 * The image is read from the SD card into user RAM (USER_BASE.., 128 KB: this spans
 * framebuffer page 1, so the display is switched to page 0 first), checked (size and
 * the mkrom.py word sum), then handed to the RAM-resident routine in flashram.S which,
 * with interrupts masked, erases/programs/verifies the flash one FLASH_SECTOR_BYTES
 * sector at a time and jumps through the new image's reset vectors.  Nothing may run
 * from the flash once the first sector is erased (this ROM is being replaced under its
 * own feet), hence no C code after the hand-over. */
#include "bios.h"
#include "fs.h"
#include "flash.h"

extern const uint8_t flash_ram_start[], flash_ram_end[];

#define IMAGE_BUF   ((uint8_t *)USER_BASE)
#define RAM_CODE    ((uint8_t *)(USER_BASE + ROM_IMAGE_SIZE))
#define RAM_CODE_MAX 1024
typedef int (*flash_ram_fn)(struct flash_op *);

_Static_assert(USER_BASE + ROM_IMAGE_SIZE + RAM_CODE_MAX <= FB_PAGE0_ADDR, "image staging area overlaps the displayed page");
_Static_assert(ROM_IMAGE_SIZE % FLASH_SECTOR_BYTES == 0, "image is not a whole number of sectors");
_Static_assert(sizeof(struct flash_op) == FO_SIZE, "flash_op layout");

/* Poll loop iterations: one iteration is ~40 clocks (3.3 us at 12 MHz); allow 30x the
 * data-sheet maximum for erase and about 10x for a byte program. */
#define ERASE_LOOPS ((uint32_t)FLASH_T_SECTOR_ERASE_MS * 10000UL)
#define PROG_LOOPS  ((uint32_t)FLASH_T_BYTE_PROGRAM_US * 3UL + 32UL)

static flash_ram_fn load_ram_code(void)
{
    size_t n = (size_t)(flash_ram_end - flash_ram_start);
    if (n > RAM_CODE_MAX) return 0;
    memcpy(RAM_CODE, flash_ram_start, n);
    return (flash_ram_fn)(uintptr_t)RAM_CODE;
}

static int run_op(flash_ram_fn fn, struct flash_op *op)
{
    uint16_t sr = irq_disable();
    int rc = fn(op);
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
    flash_ram_fn fn = load_ram_code();
    if (!fn) return 0;
    op_init(&op);
    op.op = 0;
    op.dst = ROM_BASE;
    run_op(fn, &op);
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
    if (video_page != 0) bios_video_set_page(0);
    if (read_image(path) < 0) return;
    flash_ram_fn fn = load_ram_code();
    if (!fn) { kprintf("flash: RAM routine too large\n"); return; }
    uint16_t mfr, dev;
    if (!flash_read_id(&mfr, &dev)) {
        kprintf("flash: chip ID %04x/%04x, expected %02x%02x/%02x%02x: not SST39SF040s (EPROMs fitted?)\n",
                mfr, dev, FLASH_MFR_ID, FLASH_MFR_ID, FLASH_DEV_ID, FLASH_DEV_ID);
        return;
    }
    kprintf("Reprogram the ROM (%u sectors of %u bytes) from %s? Do not press NMI. (y/N) ",
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
    fn(&op);                            /* never returns: reboots or stops */
    kprintf("flash: unexpected return from the RAM routine\n");
}
