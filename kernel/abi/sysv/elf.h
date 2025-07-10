#pragma once

#include "fs/vnode.h"
#include "memory/vm.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    VNode* file;
    uintptr_t entry_point;
    struct {
        size_t size;
        size_t offset;
        size_t count;
    } phdrs;
} ElfFile;

typedef enum {
    ELF_RESULT_OK,
    ELF_RESULT_ERR_FS,
    ELF_RESULT_ERR_NOT_ELF,
    ELF_RESULT_ERR_INVALID_CLASS,
    ELF_RESULT_ERR_INVALID_MACHINE,
    ELF_RESULT_ERR_INVALID_ENCODING,
    ELF_RESULT_ERR_INVALID_VERSION,
    ELF_RESULT_ERR_FORMAT,
    ELF_RESULT_ERR_NOT_FOUND
} ElfResult;

ElfResult elf_read(const char* path_to_elf, ElfFile** elf_file);
ElfResult elf_lookup_interpreter(ElfFile* elf_file, char** interp_path);
ElfResult elf_lookup_phdr_table(ElfFile* elf_file, uintptr_t* phdr_table);
ElfResult elf_load(ElfFile* elf_file, VmAddressSpace* as);
