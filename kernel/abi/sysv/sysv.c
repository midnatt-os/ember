#include "abi/sysv/sysv.h"

#include <stddef.h>
#include <stdint.h>

#include "abi/sysv/auxv.h"
#include "common/assert.h"
#include "memory/vm.h"
#include "lib/string.h"



uintptr_t sysv_setup_stack(VmAddressSpace* as, size_t stack_size, char** argv, char** envp, Auxv* auxv) {
    ASSERT(stack_size % PAGE_SIZE == 0);

    uintptr_t desired_base = (USERSPACE_END + 1) - stack_size;
    void* stack = vm_map_anon(as, (void*) desired_base, stack_size, VM_PROT_RW, VM_CACHING_DEFAULT, VM_FLAG_FIXED | VM_FLAG_ZERO);
    ASSERT(stack != nullptr);

    uintptr_t sp = ((uintptr_t) stack + stack_size - 1) & ~0xF;

    #define WRITE_QWORD(VALUE)                                                \
        {                                                                     \
            sp -= sizeof(uint64_t);                                           \
            uint64_t tmp = (VALUE);                                           \
            size_t copyto_count = vm_copy_to(as, sp, &tmp, sizeof(uint64_t)); \
            ASSERT(copyto_count == sizeof(uint64_t));                         \
        }

    int argc = 0;
    for(; argv[argc]; argc++) sp -= strlen(argv[argc]) + 1;
    uintptr_t arg_data = sp;

    int envc = 0;
    for(; envp[envc]; envc++) sp -= strlen(envp[envc]) + 1;
    uintptr_t env_data = sp;

    sp -= (sp - (12 + 1 + envc + 1 + argc + 1) * sizeof(uint64_t)) % 0x10;

    // Auxv
    WRITE_QWORD(0);            WRITE_QWORD(0);
    WRITE_QWORD(0);            WRITE_QWORD(X86_64_AUXV_SECURE);
    WRITE_QWORD(auxv->entry);  WRITE_QWORD(X86_64_AUXV_ENTRY);
    WRITE_QWORD(auxv->phdr);   WRITE_QWORD(X86_64_AUXV_PHDR);
    WRITE_QWORD(auxv->phent);  WRITE_QWORD(X86_64_AUXV_PHENT);
    WRITE_QWORD(auxv->phnum);  WRITE_QWORD(X86_64_AUXV_PHNUM);

    WRITE_QWORD(0);
    for(int i = envc - 1; i >= 0; i--) {
        WRITE_QWORD(env_data);
        size_t str_sz = strlen(envp[i]) + 1;
        size_t copyto_count = vm_copy_to(as, env_data, envp[i], str_sz);
        ASSERT(copyto_count == str_sz);
        env_data += str_sz;
    }

    WRITE_QWORD(0);
    for(int i = argc - 1; i >= 0; i--) {
        WRITE_QWORD(arg_data);
        size_t str_sz = strlen(argv[i]) + 1;
        size_t copyto_count = vm_copy_to(as, arg_data, argv[i], str_sz);
        ASSERT(copyto_count == str_sz);
        arg_data += str_sz;
    }

    WRITE_QWORD(argc);

    return sp;
    #undef WRITE_QWORD
}
