#pragma once

#include <stddef.h>
#include <stdint.h>

#include "abi/sysv/auxv.h"
#include "memory/vm.h"

uintptr_t sysv_setup_stack(VmAddressSpace* as, size_t stack_size, char** argv, char** envp, Auxv* auxv);
