#pragma once

#include "memory/vm.h"
#include <stdint.h>

typedef struct {
    uintptr_t entry_point;
    uint64_t phdrs_size;
    uint64_t phdrs_offset;
    uint64_t phdrs_count;
} ElfFile;

typedef enum {
    ELF_RESULT_OK,
    ELF_RESULT_ERR_FS,
    ELF_RESULT_ERR_NOT_ELF,
    ELF_RESULT_ERR_INVALID_CLASS,
    ELF_RESULT_ERR_INVALID_MACHINE,
    ELF_RESULT_ERR_INVALID_ENCODING,
    ELF_RESULT_ERR_INVALID_VERSION,
} ElfResult;

ElfResult elf_load(char* path_to_elf, VmAddressSpace* as, uintptr_t* entry);
