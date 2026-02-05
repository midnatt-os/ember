#include "sys/exec.h"

#include "common/log.h"
#include "exec.h"
#include "fs/vfs.h"
#include "lib/elf.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "mem/vm.h"
#include "sched/proc.h"

int exec_load([[maybe_unused]] process_t* proc, [[maybe_unused]] path_t path, ...) {
    return 0;
}
