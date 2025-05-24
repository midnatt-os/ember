#include "common/modules.h"
#include "lib/string.h"

#include <stddef.h>



Module* find_module(Modules* modules, const char* name) {
    for (size_t i = 0; i < modules->module_count; i++) {
        if (streq(modules->modules[i].cmdline, name)) {
            return &modules->modules[i];
        }
    }
    return nullptr;
}
