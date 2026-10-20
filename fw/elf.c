#include "bios.h"
#include "elf.h"

uint32_t elf_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint16_t elf_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

int elf_parse_header(const uint8_t *b, uint32_t len, struct elf_hdr_info *out)
{
    if (len < ELF_HDR_SIZE) return E_NOEXEC;
    if (b[0] != 0x7F || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') return E_NOEXEC;
    if (b[4] != 1 /* ELFCLASS32 */ || b[5] != 2 /* ELFDATA2MSB */ || b[6] != 1) return E_NOEXEC;
    if (elf_be16(b + 16) != 2 /* ET_EXEC */) return E_NOEXEC;
    if (elf_be16(b + 18) != 4 /* EM_68K */) return E_NOEXEC;
    if (elf_be32(b + 20) != 1) return E_NOEXEC;                 /* e_version */
    out->entry = elf_be32(b + 24);
    out->phoff = elf_be32(b + 28);
    out->phentsize = elf_be16(b + 42);
    out->phnum = elf_be16(b + 44);
    if (out->phentsize != ELF_PHDR_SIZE || out->phnum == 0 || out->phoff < ELF_HDR_SIZE)
        return E_NOEXEC;
    if (out->entry & 1) return E_NOEXEC;
    return 0;
}

void elf_parse_phdr(const uint8_t *p, struct elf_phdr *ph)
{
    ph->type = elf_be32(p + 0);
    ph->offset = elf_be32(p + 4);
    ph->vaddr = elf_be32(p + 8);
    /* p+12 paddr unused */
    ph->filesz = elf_be32(p + 16);
    ph->memsz = elf_be32(p + 20);
}

int elf_segment_fits(const struct elf_phdr *ph)
{
    if (ph->filesz > ph->memsz) return 0;
    if (ph->vaddr < USER_BASE || ph->vaddr >= USER_END) return 0;
    if (ph->memsz > USER_END - ph->vaddr) return 0;
    return 1;
}
