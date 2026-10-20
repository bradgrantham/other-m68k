/* ELF32 big-endian m68k executable parsing (pure, host-testable). */
#ifndef ELF_H
#define ELF_H
#include <stdint.h>

#define ELF_HDR_SIZE   52
#define ELF_PHDR_SIZE  32
#define ELF_PT_LOAD    1
#define ELF_MAX_SEGS   8

struct elf_hdr_info {
    uint32_t entry;
    uint32_t phoff;
    uint16_t phentsize;
    uint16_t phnum;
};

struct elf_phdr {
    uint32_t type, offset, vaddr, filesz, memsz;
};

uint32_t elf_be32(const uint8_t *p);
uint16_t elf_be16(const uint8_t *p);
/* 0 or E_NOEXEC. Accepts ELFCLASS32, ELFDATA2MSB, ET_EXEC, EM_68K, phentsize 32. */
int  elf_parse_header(const uint8_t *buf, uint32_t len, struct elf_hdr_info *out);
void elf_parse_phdr(const uint8_t *p, struct elf_phdr *ph);
/* 1 if [vaddr, vaddr+memsz) lies inside [USER_BASE, USER_END) and filesz <= memsz. */
int  elf_segment_fits(const struct elf_phdr *ph);

#endif
