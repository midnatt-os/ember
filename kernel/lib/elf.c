#include "elf.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/stack_trace.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "mem/page.h"
#include "mem/vm.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>


bool elf_validate(const void* elf, size_t size, uint16_t type) {
    if (!elf || size < sizeof(elf64_ehdr_t))
        return false;

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*) elf;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 || ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3)
        return false;

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB || ehdr->e_ident[EI_VERSION] != EV_CURRENT)
        return false;

    if (ehdr->e_type != type || ehdr->e_machine != EM_X86_64 || ehdr->e_version != EV_CURRENT)
        return false;

    if (ehdr->e_shoff == 0 || ehdr->e_shentsize != sizeof(elf64_shdr_t) || ehdr->e_shnum == 0 || ehdr->e_shstrndx >= ehdr->e_shnum)
        return false;

    const size_t table_bytes = (size_t) ehdr->e_shentsize * (size_t) ehdr->e_shnum;
    if (ehdr->e_shoff > size || table_bytes > size - ehdr->e_shoff)
        return false;

    return true;
}

elf_syms_view_t elf_get_symbols_view(const void* elf, size_t size) {
    const elf64_ehdr_t* ehdr = elf;
    ASSERT(elf_validate(elf, size, ET_EXEC));

    const uintptr_t base = (uintptr_t) elf;
    const elf64_shdr_t* shdr = (const elf64_shdr_t*) (base + ehdr->e_shoff);

    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        if (shdr[i].sh_type != SHT_SYMTAB)
            continue;

        const elf64_shdr_t* symsec = &shdr[i];
        const elf64_shdr_t* strsec = &shdr[symsec->sh_link];

        elf_syms_view_t v = {
            .symtab = (const elf64_sym_t*) (base + symsec->sh_offset),
            .sym_count = (size_t) (symsec->sh_size / sizeof(elf64_sym_t)),
            .strtab = (const char*) (base + strsec->sh_offset),
        };
        return v; // first SHT_SYMTAB is enough
    }

    ASSERT_UNREACHABLE();
}

void elf_map_segments(const void* elf, const elf64_ehdr_t* ehdr, vm_address_space_t* as, elf_image_t* out_img) {
    const elf64_phdr_t* ph = (const elf64_phdr_t*) (elf + ehdr->e_phoff);

    elf64_addr_t lo = UINT64_MAX;
    elf64_addr_t hi = 0;

    for (size_t i = 0; i < ehdr->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;

        elf64_addr_t seg_lo = (elf64_addr_t) ALIGN_DOWN(ph[i].p_vaddr, PAGE_SIZE);
        elf64_addr_t seg_hi = (elf64_addr_t) ALIGN_UP(ph[i].p_vaddr + ph[i].p_memsz, PAGE_SIZE);

        if (seg_lo < lo)
            lo = seg_lo;

        if (seg_hi > hi)
            hi = seg_hi;
    }
    ASSERT(hi > lo);

    size_t elf_size = (size_t) (hi - lo);
    void* base = vm_map_anon(as, nullptr, elf_size, PAGE_SIZE, VM_PROT_RW, VM_CACHING_WRITE_BACK, VM_FLAG_DEFAULT);
    ASSERT(base);

    uintptr_t bias = (uintptr_t) base - lo;

    for (size_t i = 0; i < ehdr->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;

        uint8_t* dst = (uint8_t*) (bias + ph[i].p_vaddr);
        size_t seg_file = (size_t) ph[i].p_filesz;
        size_t seg_mem = (size_t) ph[i].p_memsz;

        memcpy(dst, elf + ph[i].p_offset, seg_file);

        if (seg_mem > seg_file)
            memclear(dst + seg_file, seg_mem - seg_file);
    }

    *out_img = (elf_image_t) {
        .base = base,
        .size = elf_size,
        .bias = bias,
        .link_lo = lo,
        .link_hi = hi,
    };
}

bool elf_dyn_parse(const void* elf, const elf64_ehdr_t* ehdr, const elf_image_t* img, dyn_info_t* di_out) {
    *di_out = (dyn_info_t) { 0 };

    const elf64_phdr_t* ph = (const elf64_phdr_t*) (elf + ehdr->e_phoff);
    const elf64_phdr_t* dyn_ph = nullptr;

    for (elf64_half_t i = 0; i < ehdr->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn_ph = &ph[i];
            break;
        }
    }
    if (!dyn_ph || dyn_ph->p_memsz < sizeof(elf64_dyn_t))
        return false;

    const elf64_dyn_t* dyn = (const elf64_dyn_t*) (img->bias + dyn_ph->p_vaddr);
    size_t dyn_cnt = (size_t) (dyn_ph->p_memsz / sizeof(elf64_dyn_t));

    const elf64_sym_t* dynsym = nullptr;
    size_t syment = 0;

    const char* dynstr = nullptr;
    size_t strsz = 0;

    const elf64_rela_t* rela = nullptr;
    size_t relasz = 0;
    size_t relaent = 0;

    const elf64_rela_t* jmprel = nullptr;
    size_t jmprelsz = 0;
    elf64_xword_t pltrel = 0;

    const uint32_t* sysv_hash = nullptr; // DT_HASH (optional, gives dynsym_cnt)

    for (size_t i = 0; i < dyn_cnt; ++i) {
        switch (dyn[i].d_tag) {
            case DT_NULL: i = dyn_cnt; break;

            case DT_SYMTAB: dynsym = (const elf64_sym_t*) (img->bias + dyn[i].d_un.d_ptr); break;
            case DT_SYMENT: syment = (size_t) dyn[i].d_un.d_val; break;

            case DT_STRTAB: dynstr = (const char*) (img->bias + dyn[i].d_un.d_ptr); break;
            case DT_STRSZ:  strsz = (size_t) dyn[i].d_un.d_val; break;

            case DT_RELA:    rela = (const elf64_rela_t*) (img->bias + dyn[i].d_un.d_ptr); break;
            case DT_RELASZ:  relasz = (size_t) dyn[i].d_un.d_val; break;
            case DT_RELAENT: relaent = (size_t) dyn[i].d_un.d_val; break;

            case DT_JMPREL:   jmprel = (const elf64_rela_t*) (img->bias + dyn[i].d_un.d_ptr); break;
            case DT_PLTRELSZ: jmprelsz = (size_t) dyn[i].d_un.d_val; break;
            case DT_PLTREL:   pltrel = (elf64_xword_t) dyn[i].d_un.d_val; break;

            case DT_HASH: sysv_hash = (const uint32_t*) (img->bias + dyn[i].d_un.d_ptr); break;

            default: break;
        }
    }

    if (!dynsym || !dynstr)
        return false;

    if (syment && syment != sizeof(elf64_sym_t))
        return false;

    size_t rela_cnt = 0, jmprel_cnt = 0;

    if (rela) {
        size_t ent = relaent ? relaent : sizeof(elf64_rela_t);
        if (ent != sizeof(elf64_rela_t) || (relasz % ent) != 0)
            return false;
        rela_cnt = relasz / ent;
    }

    if (jmprel || jmprelsz) {
        if (pltrel != DT_RELA)
            return false; // only RELA supported

        if ((jmprelsz % sizeof(elf64_rela_t)) != 0)
            return false;

        jmprel_cnt = jmprelsz / sizeof(elf64_rela_t);
    }

    size_t dynsym_cnt = 0;

    if (sysv_hash) {
        // SysV hash layout: [nbucket][nchain]...
        dynsym_cnt = (size_t) sysv_hash[1];
    }

    *di_out = (dyn_info_t) {
        .dynsym = dynsym,
        .dynstr = dynstr,
        .dynstr_size = strsz,
        .dynsym_cnt = dynsym_cnt,
        .rela = rela,
        .rela_cnt = rela_cnt,
        .jmprel = jmprel,
        .jmprel_cnt = jmprel_cnt,
    };

    return true;
}

static uintptr_t sym_runtime_value(const elf64_sym_t* s, uintptr_t bias, const char* dynstr, size_t dynstr_size) {
    // Defined symbols: bias + value (or absolute)
    if (s->st_shndx != SHN_UNDEF) {
        return (s->st_shndx == SHN_ABS) ? s->st_value : bias + s->st_value;
    }

    // Undefined → resolve from kernel
    const char* name = (s->st_name < dynstr_size) ? dynstr + s->st_name : NULL;
    if (!name || !*name)
        return 0;

    uintptr_t val = kernel_symbol_lookup(name);
    ASSERT(val || ELF64_ST_BIND(s->st_info) == STB_WEAK);

    return val;
}

bool elf_apply_relocations(const dyn_info_t* di, const elf_image_t* img) {
    const elf64_rela_t* tables[2] = { di->rela, di->jmprel };
    const size_t counts[2] = { di->rela_cnt, di->jmprel_cnt };

    for (size_t t = 0; t < 2; t++) {
        const elf64_rela_t* rtab = tables[t];
        size_t n = counts[t];
        if (!rtab || !n)
            continue;

        for (size_t i = 0; i < n; i++) {
            const elf64_rela_t* r = &rtab[i];
            uint32_t type = ELF64_R_TYPE(r->r_info);
            uint32_t si = ELF64_R_SYM(r->r_info);

            uint8_t* loc = (uint8_t*) (img->bias + r->r_offset);
            int64_t A = r->r_addend;
            uintptr_t P = (uintptr_t) loc;

            uintptr_t S = 0;
            if (type != R_X86_64_NONE && type != R_X86_64_RELATIVE)
                S = sym_runtime_value(&di->dynsym[si], img->bias, di->dynstr, di->dynstr_size);

            switch (type) {
                case R_X86_64_NONE: break;

                case R_X86_64_RELATIVE: *(uint64_t*) loc = img->bias + A; break;

                case R_X86_64_64:
                case R_X86_64_GLOB_DAT:
                case R_X86_64_JUMP_SLOT: *(uint64_t*) loc = S + A; break;

                case R_X86_64_PC32:
                case R_X86_64_PLT32: {
                    int64_t disp = (int64_t) (S + A - P);
                    ASSERT(disp >= INT32_MIN && disp <= INT32_MAX);

                    *(int32_t*) loc = (int32_t) disp;
                    break;
                }

                case R_X86_64_32: {
                    uint64_t val = S + A;
                    ASSERT(val <= UINT32_MAX);
                    *(uint32_t*) loc = (uint32_t) val;
                    break;
                }

                case R_X86_64_32S: {
                    int64_t sval = (int64_t) (S + A);
                    ASSERT(sval >= INT32_MIN && sval <= INT32_MAX);
                    *(int32_t*) loc = (int32_t) sval;
                    break;
                }

                default: ASSERT_UNREACHABLE();
            }
        }
    }

    return true;
}

static inline vm_prot_t prot_from_pflags_min(elf64_word_t pf) {
    vm_prot_t p = { 0 };
    p.read = (pf & PF_R) != 0;
    p.write = (pf & PF_W) != 0;
    p.execute = (pf & PF_X) != 0;

    if (p.execute && !p.read)
        p.read = true;

    if (p.execute && p.write)
        p.write = false;

    return p;
}

void elf_finalize_protections(const void* elf, const elf64_ehdr_t* ehdr, const elf_image_t* img, vm_address_space_t* as) {
    const elf64_phdr_t* ph = (const elf64_phdr_t*) (elf + ehdr->e_phoff);

    for (elf64_half_t i = 0; i < ehdr->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD)
            continue;

        uintptr_t seg_lo = ALIGN_DOWN((uintptr_t) img->bias + (uintptr_t) ph[i].p_vaddr, PAGE_SIZE);
        uintptr_t seg_hi = ALIGN_UP((uintptr_t) img->bias + (uintptr_t) ph[i].p_vaddr + (uintptr_t) ph[i].p_memsz, PAGE_SIZE);
        size_t len = seg_hi - seg_lo;

        vm_prot_t want = prot_from_pflags_min(ph[i].p_flags);

        vm_protect(as, (void*) seg_lo, len, want);
    }
}

// --- Step 6: Find init/deinit ---
uintptr_t elf_find_func(const dyn_info_t* di, const elf_image_t* img, const char* want) {
    ASSERT(di->dynsym && di->dynstr && di->dynsym_cnt);

    for (size_t i = 0; i < di->dynsym_cnt; ++i) {
        const elf64_sym_t* s = &di->dynsym[i];

        if (ELF64_ST_TYPE(s->st_info) != STT_FUNC)
            continue;

        if (s->st_shndx == SHN_UNDEF)
            continue;

        const char* name = (s->st_name < di->dynstr_size) ? di->dynstr + s->st_name : nullptr;

        if (!name || !*name)
            continue;

        if (!streq(name, want))
            continue;

        return (s->st_shndx == SHN_ABS) ? s->st_value : img->bias + s->st_value;
    }

    return 0;
}
