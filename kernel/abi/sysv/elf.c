#include "abi/sysv/elf.h"

#include <stddef.h>

#include "common/assert.h"
#include "common/log.h"
#include "common/types.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "lib/mem.h"
#include "memory/heap.h"
#include "common/util.h"
#include "memory/vm.h"
#include "nanoprintf.h"


#define ELF64_ID0 0x7F
#define ELF64_ID1 'E'
#define ELF64_ID2 'L'
#define ELF64_ID3 'F'
#define ELF64_ID_VALID(ID) ((ID)[0] == ELF64_ID0 && (ID)[1] == ELF64_ID1 && (ID)[2] == ELF64_ID2 && (ID)[3] == ELF64_ID3)

#define ELF64_CLASS64     2
#define ELF64_EM_X86_64   62  // AMD x86-64 architecture
#define ELF64_DATA2_LSB   1   // Little-endian
#define ELF64_VER_CURRENT 1   // Current version

#define ELF64_PF_X 0x1
#define ELF64_PF_W 0x2
#define ELF64_PF_R 0x4

#define ELF64_PT_PHDR 6

#define ELF64_PT_NULL    0
#define ELF64_PT_LOAD    1
#define ELF64_PT_DYNAMIC 2
#define ELF64_PT_INTERP  3
#define ELF64_PT_NOTE    4

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

ElfResult elf_read(const char* path_to_elf, ElfFile** elf_file) {
    VNode* elf_vnode;
    if (vfs_lookup(nullptr, path_to_elf, &elf_vnode) < 0)
        return ELF_RESULT_ERR_FS;

    Attributes file_attr;

    if (elf_vnode->ops->get_attr(elf_vnode, &file_attr) < 0)
        return ELF_RESULT_ERR_FS;

    Elf64FileHdr elf_file_hdr;

    ssize_t res;
    if ((res = elf_vnode->ops->read(elf_vnode, &elf_file_hdr, sizeof(Elf64FileHdr), 0)) < 0 || res != sizeof(Elf64FileHdr))
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
        .file = elf_vnode,
        .entry_point = elf_file_hdr.e_entry,
        .phdrs = {
            .size = elf_file_hdr.e_phentsize,
            .offset = elf_file_hdr.e_phoff,
            .count = elf_file_hdr.e_phnum
        }
    };

    return ELF_RESULT_OK;
}

static bool read_phdr(ElfFile* elf_file, size_t index, Elf64ProgHdr* phdr) {
    ssize_t read_res = elf_file->file->ops->read(
        elf_file->file,
        phdr,
        sizeof(Elf64ProgHdr),
        elf_file->phdrs.offset + (index * elf_file->phdrs.size)
    );

    return (read_res > 0 || read_res == sizeof(Elf64ProgHdr));
}

ElfResult elf_lookup_interpreter(ElfFile* elf_file, char** interp_path) {
    Elf64ProgHdr* phdr = kmalloc(sizeof(Elf64ProgHdr));
    for (size_t i = 0; i < elf_file->phdrs.count; i++) {
        if (!read_phdr(elf_file, i, phdr))
            return ELF_RESULT_ERR_FS;

        if (phdr->type != ELF64_PT_INTERP)
            continue;

        size_t interpreter_size = phdr->filesz + 1;
        char* interp = kmalloc(interpreter_size);
        memclear(interp, interpreter_size);

        ssize_t read_res = elf_file->file->ops->read(
            elf_file->file,
            interp,
            phdr->filesz,
            phdr->offset
        );

        if(read_res < 0 || read_res != (ssize_t) phdr->filesz) {
            kfree(interp);
            return ELF_RESULT_ERR_FS;
        }

        *interp_path = interp;
        return ELF_RESULT_OK;

    }

    return ELF_RESULT_ERR_NOT_FOUND;
}

ElfResult elf_lookup_phdr_table(ElfFile* elf_file, uintptr_t* phdr_table) {
    Elf64ProgHdr* phdr = kmalloc(sizeof(Elf64ProgHdr));
    for (size_t i = 0; i < elf_file->phdrs.count; i++) {
        if (!read_phdr(elf_file, i, phdr))
            return ELF_RESULT_ERR_FS;

        if (phdr->type != ELF64_PT_PHDR)
            continue;

        *phdr_table = phdr->vaddr;
        return ELF_RESULT_OK;
    }

    return ELF_RESULT_ERR_NOT_FOUND;
}

ElfResult elf_load(ElfFile* elf_file, VmAddressSpace* as) {
    Elf64ProgHdr* phdr = kmalloc(sizeof(Elf64ProgHdr));
    for (size_t i = 0; i < elf_file->phdrs.count; i++) {
        if (!read_phdr(elf_file, i, phdr))
            return ELF_RESULT_ERR_FS;

        switch (phdr->type) {
            case ELF64_PT_LOAD:
                uintptr_t aligned_vaddr = ALIGN_DOWN(phdr->vaddr, PAGE_SIZE);
                size_t length = ALIGN_UP(phdr->memsz + (phdr->vaddr - aligned_vaddr), PAGE_SIZE);

                VmProtection prot = {
                    .read  = (phdr->flags & ELF64_PF_R) != 0,
                    .write = (phdr->flags & ELF64_PF_W) != 0,
                    .exec  = (phdr->flags & ELF64_PF_X) != 0
                };

                vm_map_anon(as, (void*) aligned_vaddr, length, prot, VM_CACHING_DEFAULT, VM_FLAG_FIXED | VM_FLAG_ZERO);

                if (phdr->filesz <= 0)
                    break;

                size_t buf_size = ALIGN_UP(phdr->filesz, PAGE_SIZE);
                void* buf = vm_map_anon(&kernel_as, nullptr, buf_size, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_NONE);
                ASSERT(buf != nullptr);

                ssize_t read_res = elf_file->file->ops->read(elf_file->file, buf, phdr->filesz, phdr->offset);
                if (read_res < 0 || read_res != (ssize_t) phdr->filesz) {
                    vm_unmap(&kernel_as, buf, buf_size);
                    return ELF_RESULT_ERR_FS;
                }

                size_t copied = vm_copy_to(as, phdr->vaddr, buf, phdr->filesz);
                ASSERT(copied == phdr->filesz);

                vm_unmap(&kernel_as, buf, buf_size);

                break;

            case ELF64_PT_NULL:   break;
            case ELF64_PT_INTERP: break;
            case ELF64_PT_PHDR:   break;
            default: break; // log(LOG_WARN, "ELF", "Ignoring program header %#x", phdr->type);
        }
    }

    return ELF_RESULT_OK;
}
