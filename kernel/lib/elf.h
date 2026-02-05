#pragma once

#include "fs/vfs.h"
#include "mem/vm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint16_t elf64_half_t;
typedef uint32_t elf64_word_t;
typedef int32_t elf64_sword_t;
typedef uint64_t elf64_xword_t;
typedef int64_t elf64_sxword_t;
typedef uint64_t elf64_addr_t;
typedef uint64_t elf64_off_t;

typedef struct {
    unsigned char e_ident[16];
    elf64_half_t e_type;
    elf64_half_t e_machine;
    elf64_word_t e_version;
    elf64_addr_t e_entry;
    elf64_off_t e_phoff;
    elf64_off_t e_shoff;
    elf64_word_t e_flags;
    elf64_half_t e_ehsize;
    elf64_half_t e_phentsize;
    elf64_half_t e_phnum;
    elf64_half_t e_shentsize;
    elf64_half_t e_shnum;
    elf64_half_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    elf64_word_t p_type; // Segment type (PT_LOAD, PT_DYNAMIC, etc.)
    elf64_word_t p_flags; // Segment flags (PF_R, PF_W, PF_X)
    elf64_off_t p_offset; // Offset of the segment in the file
    elf64_addr_t p_vaddr; // Virtual address of the segment in memory
    elf64_addr_t p_paddr; // Physical address (ignored on x86_64)
    elf64_xword_t p_filesz; // Size of the segment in the file
    elf64_xword_t p_memsz; // Size of the segment in memory
    elf64_xword_t p_align; // Alignment (power of two, usually page size)
} elf64_phdr_t;

typedef struct {
    elf64_word_t sh_name;
    elf64_word_t sh_type;
    elf64_xword_t sh_flags;
    elf64_addr_t sh_addr;
    elf64_off_t sh_offset;
    elf64_xword_t sh_size;
    elf64_word_t sh_link;
    elf64_word_t sh_info;
    elf64_xword_t sh_addralign;
    elf64_xword_t sh_entsize;
} elf64_shdr_t;

typedef struct {
    elf64_addr_t r_offset;
    elf64_xword_t r_info;
} elf64_rel_t;

typedef struct {
    elf64_addr_t r_offset;
    elf64_xword_t r_info;
    elf64_sxword_t r_addend;
} elf64_rela_t;

typedef struct {
    const char* name;
    elf64_word_t type;
    elf64_xword_t flags;
    elf64_xword_t size;
    elf64_xword_t align;
    elf64_word_t link;
    elf64_word_t info;
    elf64_off_t offset;
    elf64_xword_t entsize;

    // Populated after mapping
    void* runtime_addr;
    size_t runtime_size;
    vm_prot_t desired_prot;
} elf_section_info_t;

typedef struct {
    const elf64_rela_t* entries;
    size_t count;
    uint16_t target_index;
} elf_rela_info_t;

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_ABIVERSION 8

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3

#define EM_X86_64 62

#define PT_LOAD 1
#define PT_INTERP 3
#define PT_PHDR 6

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_DYNSYM 11

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

#define ELF64_R_SYM(info) ((uint32_t) ((info) >> 32))
#define ELF64_R_TYPE(info) ((uint32_t) (info))
#define ELF64_R_INFO(sym, type) ((((uint64_t) (sym)) << 32) | ((uint64_t) (type) & 0xffffffffu))

#define ELF64_ST_BIND(info) ((info) >> 4)
#define ELF64_ST_TYPE(info) ((info) & 0xf)
#define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4
#define R_X86_64_COPY 5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_32 10
#define R_X86_64_32S 11

typedef struct {
    elf64_sxword_t d_tag;
    union {
        elf64_xword_t d_val;
        elf64_addr_t d_ptr;
    } d_un;
} elf64_dyn_t;

// DT_* we actually use
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_SYMBOLIC 16
#define DT_REL 17 // (unsupported here)
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20 // value == DT_RELA or DT_REL
#define DT_DEBUG 21
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
// GNU extensions
#define DT_GNU_RELRO 0x6ffffdfc
#define PT_GNU_RELRO 0x6474e552u
// p_type
#define PT_DYNAMIC 2

#define AT_NULL 0 /* End of vector */
#define AT_IGNORE 1 /* Entry should be ignored */
#define AT_EXECFD 2 /* File descriptor of program */
#define AT_PHDR 3 /* Program headers for program */
#define AT_PHENT 4 /* Size of program header entry */
#define AT_PHNUM 5 /* Number of program headers */
#define AT_PAGESZ 6 /* System page size */
#define AT_BASE 7 /* Base address of interpreter */
#define AT_FLAGS 8 /* Flags */
#define AT_ENTRY 9 /* Entry point of program */
#define AT_NOTELF 10 /* Program is not ELF */
#define AT_UID 11 /* Real uid */
#define AT_EUID 12 /* Effective uid */
#define AT_GID 13 /* Real gid */
#define AT_EGID 14 /* Effective gid */
#define AT_PLATFORM 15 /* String identifying platform */
#define AT_HWCAP 16 /* Machine-dependent hints about processor capabilities */
#define AT_CLKTCK 17 /* Frequency at which times() increments */
#define AT_SECURE 23 /* Boolean, non-zero if OS is in "secure-execution" mode */
#define AT_BASE_PLATFORM 24 /* String identifying real platform, may differ from AT_PLATFORM */
#define AT_RANDOM 25 /* Address of 16 random bytes */
#define AT_HWCAP2 26 /* Extension of AT_HWCAP */
#define AT_EXECFN 31 /* Filename of program */

typedef struct {
    elf64_word_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    elf64_half_t st_shndx;
    elf64_addr_t st_value;
    elf64_xword_t st_size;
} elf64_sym_t;

typedef struct {
    const elf64_sym_t* symtab;
    size_t sym_count;
    const char* strtab;
} elf_syms_view_t;

typedef struct {
    const elf64_sym_t* dynsym;
    const char* dynstr;
    size_t dynstr_size; // from DT_STRSZ (for bounds)
    size_t dynsym_cnt; // from DT_HASH.nchain (optional; 0 if unknown)

    const elf64_rela_t* rela;
    size_t rela_cnt;

    const elf64_rela_t* jmprel;
    size_t jmprel_cnt;
} dyn_info_t;

typedef struct {
    void* base;
    size_t size;
    uintptr_t bias;
    elf64_addr_t link_lo;
    elf64_addr_t link_hi;
} elf_image_t;

typedef struct {
    uint64_t phdr;
    uint64_t phent;
    uint64_t phnum;
    uint64_t entry;
    uint64_t base;
    uint64_t pagesz;
    uint64_t secure;
} auxv64list_t;

typedef struct {
    uintptr_t entry_point;
    uintptr_t phdr_vaddr;
    uint16_t phnum;
    uint16_t phentsize;
    char* interpreter_path;
    uintptr_t load_bias;
} elf_info_t;


static inline const char* elf_sym_name(const elf_syms_view_t* v, const elf64_sym_t* s) {
    return v->strtab + s->st_name;
}

bool elf_validate(const elf64_ehdr_t* ehdr);
elf_syms_view_t elf_get_symbols_view(const void* image);

int elf_load(path_t path, vm_address_space_t* as, elf_info_t* out_info, uintptr_t load_bias);
uintptr_t elf_prepare_stack(vm_address_space_t* as, elf_info_t* prog_info, elf_info_t* interp_info, char** argv, char** envp);

void elf_map_segments(const void* elf, const elf64_ehdr_t* ehdr, vm_address_space_t* as, elf_image_t* out_img);
bool elf_dyn_parse(const void* elf, const elf64_ehdr_t* ehdr, const elf_image_t* img, dyn_info_t* di_out);
bool elf_apply_relocations(const dyn_info_t* di, const elf_image_t* img);
void elf_finalize_protections(const void* elf, const elf64_ehdr_t* ehdr, const elf_image_t* img, vm_address_space_t* as);
uintptr_t elf_find_func(const dyn_info_t* di, const elf_image_t* img, const char* want);
