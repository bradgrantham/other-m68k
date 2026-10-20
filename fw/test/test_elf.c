/* ELF header / program header parsing: a synthetic big-endian m68k ELF and, when the
 * path is given and exists, the SDK's hello.elf. */
#include "test.h"
#include "elf.h"

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static void make_elf(uint8_t *b, uint32_t entry, int phnum)
{
    memset(b, 0, ELF_HDR_SIZE + phnum * ELF_PHDR_SIZE);
    b[0] = 0x7F; b[1] = 'E'; b[2] = 'L'; b[3] = 'F'; b[4] = 1; b[5] = 2; b[6] = 1;
    be16(b + 16, 2); be16(b + 18, 4); be32(b + 20, 1);
    be32(b + 24, entry); be32(b + 28, ELF_HDR_SIZE);
    be16(b + 40, ELF_HDR_SIZE); be16(b + 42, ELF_PHDR_SIZE); be16(b + 44, (uint16_t)phnum);
}

static void make_phdr(uint8_t *p, uint32_t type, uint32_t off, uint32_t vaddr, uint32_t filesz, uint32_t memsz)
{
    be32(p + 0, type); be32(p + 4, off); be32(p + 8, vaddr); be32(p + 12, vaddr);
    be32(p + 16, filesz); be32(p + 20, memsz); be32(p + 24, 7); be32(p + 28, 2);
}

void test_elf(const char *hello_elf)
{
    uint8_t img[ELF_HDR_SIZE + 3 * ELF_PHDR_SIZE];
    struct elf_hdr_info hi;
    struct elf_phdr ph;

    make_elf(img, 0x4000, 2);
    make_phdr(img + ELF_HDR_SIZE, ELF_PT_LOAD, 0x1000, 0x4000, 0x800, 0xA00);
    make_phdr(img + ELF_HDR_SIZE + ELF_PHDR_SIZE, 4 /* PT_NOTE */, 0, 0, 0x10, 0x10);
    CHECK_EQ(elf_parse_header(img, sizeof img, &hi), 0);
    CHECK_EQ(hi.entry, 0x4000); CHECK_EQ(hi.phnum, 2); CHECK_EQ(hi.phoff, ELF_HDR_SIZE); CHECK_EQ(hi.phentsize, 32);
    elf_parse_phdr(img + hi.phoff, &ph);
    CHECK_EQ(ph.type, ELF_PT_LOAD); CHECK_EQ(ph.offset, 0x1000); CHECK_EQ(ph.vaddr, 0x4000);
    CHECK_EQ(ph.filesz, 0x800); CHECK_EQ(ph.memsz, 0xA00);
    CHECK(elf_segment_fits(&ph));
    elf_parse_phdr(img + hi.phoff + ELF_PHDR_SIZE, &ph);
    CHECK_EQ(ph.type, 4);

    /* range checks */
    ph.type = ELF_PT_LOAD; ph.filesz = 0x100;
    ph.vaddr = USER_BASE; ph.memsz = USER_SIZE; CHECK(elf_segment_fits(&ph));          /* exactly fills user RAM */
    ph.memsz = USER_SIZE + 1; CHECK(!elf_segment_fits(&ph));
    ph.vaddr = USER_BASE - 2; ph.memsz = 0x100; CHECK(!elf_segment_fits(&ph));          /* BIOS data */
    ph.vaddr = FB1_BASE; CHECK(!elf_segment_fits(&ph));                                  /* framebuffer */
    ph.vaddr = FB0_BASE; CHECK(!elf_segment_fits(&ph));
    ph.vaddr = 0; CHECK(!elf_segment_fits(&ph));                                         /* vectors */
    ph.vaddr = 0xE00000; CHECK(!elf_segment_fits(&ph));                                  /* ROM */
    ph.vaddr = 0xFFFFFF00; ph.memsz = 0x200; CHECK(!elf_segment_fits(&ph));              /* wrap */
    ph.vaddr = USER_BASE; ph.memsz = 0x100; ph.filesz = 0x101; CHECK(!elf_segment_fits(&ph)); /* filesz > memsz */

    /* header rejections */
    CHECK_EQ(elf_parse_header(img, ELF_HDR_SIZE - 1, &hi), E_NOEXEC);
    make_elf(img, 0x4000, 1); img[5] = 1; CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC);  /* little endian */
    make_elf(img, 0x4000, 1); img[4] = 2; CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC);  /* ELF64 */
    make_elf(img, 0x4000, 1); be16(img + 18, 3); CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC); /* x86 */
    make_elf(img, 0x4000, 1); be16(img + 16, 3); CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC); /* ET_DYN */
    make_elf(img, 0x4000, 1); be16(img + 42, 56); CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC); /* phentsize */
    make_elf(img, 0x4000, 0); CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC);   /* no phdrs */
    make_elf(img, 0x4001, 1); CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC);   /* odd entry */
    img[1] = 'X'; CHECK_EQ(elf_parse_header(img, sizeof img, &hi), E_NOEXEC);
    uint8_t srec[64] = "S00F000068656C6C6F202020202000003C";
    CHECK_EQ(elf_parse_header(srec, sizeof srec, &hi), E_NOEXEC);

    /* the real thing */
    if (!hello_elf) { printf("test_elf: no hello.elf path given, skipping\n"); return; }
    FILE *f = fopen(hello_elf, "rb");
    if (!f) { printf("test_elf: %s not found, skipping (make -C sdk)\n", hello_elf); return; }
    static uint8_t file[256 * 1024];
    size_t n = fread(file, 1, sizeof file, f);
    fclose(f);
    CHECK_EQ(elf_parse_header(file, (uint32_t)n, &hi), 0);
    CHECK_EQ(hi.entry, USER_BASE);
    CHECK(hi.phnum >= 1);
    int loads = 0;
    for (unsigned i = 0; i < hi.phnum; i++) {
        elf_parse_phdr(file + hi.phoff + i * ELF_PHDR_SIZE, &ph);
        if (ph.type != ELF_PT_LOAD) continue;
        loads++;
        CHECK(elf_segment_fits(&ph));
        CHECK(ph.offset + ph.filesz <= n);
        CHECK_EQ(ph.vaddr, USER_BASE);
        CHECK(ph.memsz >= ph.filesz);
        /* first bytes of .text are crt0's `lea __bss_start,%a0` (0x41f9) */
        CHECK_EQ(file[ph.offset], 0x41); CHECK_EQ(file[ph.offset + 1], 0xF9);
    }
    CHECK_EQ(loads, 1);
}
