#include "abi/sysv/elf.h"

#include <stddef.h>

#include "common/assert.h"
#include "common/log.h"
#include "fs/vfs.h"
#include "memory/heap.h"
#include "common/util.h"
#include "memory/vm.h"
#include "nanoprintf.h"


#define ELF64_ID0 0x7F
#define ELF64_ID1 'E'
#define ELF64_ID2 'L'
#define ELF64_ID3 'F'
#define ELF64_ID_VALID(ID) ((ID)[0] == ELF64_ID0 && (ID)[1] == ELF64_ID1 && (ID)[2] == ELF64_ID2 && (ID)[3] == ELF64_ID3)

#define ELF64_CLASS64 2
#define ELF64_EM_X86_64   62  // AMD x86-64 architecture
#define ELF64_DATA2_LSB   1         // Little-endian
#define ELF64_VER_CURRENT  1   // Current version
#define ELF64_PT_LOAD  1

#define ELF64_PF_X 0x1
#define ELF64_PF_W 0x2
#define ELF64_PF_R 0x4

typedef uint64_t elf64_addr_t;
typedef uint64_t elf64_off_t;
typedef uint16_t elf64_half_t;
typedef uint32_t elf64_word_t;
typedef int32_t  elf64_sword_t;
typedef uint64_t elf64_xword_t;
typedef int64_t  elf64_sxword_t;

typedef struct [[gnu::packed]] {
    unsigned char e_ident[16];  // ELF identification
    elf64_half_t  e_type;       // Object file type
    elf64_half_t  e_machine;    // Machine type
    elf64_word_t  e_version;    // Object file version
    elf64_addr_t  e_entry;      // Entry point address
    elf64_off_t   e_phoff;      // Program header offset
    elf64_off_t   e_shoff;      // Section header offset
    elf64_word_t  e_flags;      // Processor-specific flags
    elf64_half_t  e_ehsize;     // ELF header size
    elf64_half_t  e_phentsize;  // Size of program header entry
    elf64_half_t  e_phnum;      // Number of program header entries
    elf64_half_t  e_shentsize;  // Size of section header entry
    elf64_half_t  e_shnum;      // Number of section header entries
    elf64_half_t  e_shstrndx;   // Section name string table index
} Elf64FileHdr;

typedef struct [[gnu::packed]] {
    elf64_word_t  type;
    elf64_word_t  flags;
    elf64_off_t   offset;
    elf64_addr_t  vaddr;
    elf64_addr_t  paddr;
    elf64_xword_t filesz;
    elf64_xword_t memsz;
    elf64_xword_t align;
} Elf64ProgHdr;

static ElfResult elf_read(VNode* elf_vnode, ElfFile** elf_file) {
    VNodeAttributes file_attr;

    if (elf_vnode->ops->get_attr(elf_vnode, &file_attr) != VFS_RES_OK)
        return ELF_RESULT_ERR_FS;

    Elf64FileHdr elf_file_hdr;

    size_t read_count;
    if (elf_vnode->ops->read(elf_vnode, &elf_file_hdr, 0, sizeof(Elf64FileHdr), &read_count) != VFS_RES_OK
        || read_count != sizeof(Elf64FileHdr))
        return ELF_RESULT_ERR_FS;

    if (!ELF64_ID_VALID(elf_file_hdr.e_ident))
        return ELF_RESULT_ERR_NOT_ELF;

    if (elf_file_hdr.e_ident[4] != ELF64_CLASS64)
        return ELF_RESULT_ERR_INVALID_CLASS;

    if (elf_file_hdr.e_machine != ELF64_EM_X86_64)
        return ELF_RESULT_ERR_INVALID_MACHINE;

    if (elf_file_hdr.e_ident[5] != ELF64_DATA2_LSB)
        return ELF_RESULT_ERR_INVALID_ENCODING;

    if (elf_file_hdr.e_version != ELF64_VER_CURRENT)
        return ELF_RESULT_ERR_INVALID_VERSION;

    *elf_file = kmalloc(sizeof(ElfFile));
    **elf_file = (ElfFile) {
        .entry_point = elf_file_hdr.e_entry,
        .phdrs_size = elf_file_hdr.e_phentsize,
        .phdrs_offset = elf_file_hdr.e_phoff,
        .phdrs_count = elf_file_hdr.e_phnum
    };

    return ELF_RESULT_OK;
}

ElfResult elf_load(char* path_to_elf, VmAddressSpace* as, uintptr_t* entry) {
    ElfFile* elf_file;
    ElfResult elf_res;

    VNode* elf_vnode;

    if (vfs_lookup((char*) path_to_elf, &elf_vnode) != VFS_RES_OK)
        return ELF_RESULT_ERR_FS;

    if ((elf_res = elf_read(elf_vnode, &elf_file)) != ELF_RESULT_OK)
        return elf_res;

    for (size_t i = 0; i < elf_file->phdrs_count; i++) {
        Elf64ProgHdr ph;
        size_t read_count;
        elf64_off_t off = elf_file->phdrs_offset + i * elf_file->phdrs_size;

        if (elf_vnode->ops->read(elf_vnode, &ph, off, sizeof(Elf64ProgHdr), &read_count) != VFS_RES_OK
            || read_count != sizeof(Elf64ProgHdr))
            return ELF_RESULT_ERR_FS;

        if (ph.type != ELF64_PT_LOAD)
            continue;

        uintptr_t seg_start   = ALIGN_DOWN(ph.vaddr, PAGE_SIZE);
        uintptr_t seg_end     = ALIGN_UP(ph.paddr + ph.memsz, PAGE_SIZE);
        size_t    seg_mem_sz  = seg_end - seg_start;

        VmProtection prot = {
            .read  = (ph.flags & ELF64_PF_R) != 0,
            .write = (ph.flags & ELF64_PF_W) != 0,
            .exec  = (ph.flags & ELF64_PF_X) != 0
        };

        ASSERT(vm_map_anon(as, (void*) seg_start, seg_mem_sz, prot, VM_CACHING_DEFAULT, VM_FLAG_FIXED | VM_FLAG_ZERO) != nullptr);

        uint8_t *tmp = kmalloc(ph.filesz);

        if (elf_vnode->ops->read(elf_vnode, tmp, ph.offset, ph.filesz, &read_count) != VFS_RES_OK || read_count != ph.filesz) {
            kfree(tmp);
            return ELF_RESULT_ERR_FS;
        }

        size_t copied = vm_copy_to(as, ph.vaddr, tmp, ph.filesz);
        kfree(tmp);

        if (copied != ph.filesz)
            return ELF_RESULT_ERR_FS;
    }

    *entry = elf_file->entry_point;
    return ELF_RESULT_OK;
}
