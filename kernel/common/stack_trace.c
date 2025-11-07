#include "stack_trace.h"

#include "common/limine_requests.h"
#include "common/log.h"
#include "lib/elf.h"
#include "lib/string.h"

#include <stdint.h>


elf_syms_view_t syms_view;

void load_kernel_symbols() {
    struct limine_file* kernel_file = executable_file_request.response->executable_file;
    syms_view = elf_get_symbols_view(kernel_file->address, kernel_file->size);
}

static inline int sym_is_candidate(const elf64_sym_t* s) {
    if (s->st_shndx == SHN_UNDEF)
        return 0; // imports
    unsigned t = ELF64_ST_TYPE(s->st_info);
    // Keep FUNC/OBJECT; include NOTYPE to catch asm labels without type
    return (t == STT_FUNC || t == STT_OBJECT || t == STT_NOTYPE);
}

static inline const char* sym_name(const elf64_sym_t* s) {
    return syms_view.strtab + s->st_name;
}

// ---- RIP -> symbol ----
// Returns true if found; fills out_name/out_base/out_size if non-NULL
bool kernel_find_symbol(uintptr_t rip, const char** out_name, uintptr_t* out_base, size_t* out_size) {
    const elf64_sym_t* best = NULL;
    uintptr_t best_base = 0;
    size_t best_size = 0;

    const elf64_sym_t* tab = syms_view.symtab;
    size_t n = syms_view.sym_count;

    for (size_t i = 0; i < n; ++i) {
        const elf64_sym_t* s = &tab[i];
        if (!sym_is_candidate(s))
            continue;

        uintptr_t base = (uintptr_t) s->st_value; // add slide here if you have KASLR
        if (base > rip)
            continue;

        size_t sz = (size_t) s->st_size;

        int best_covers = (best && best_size && rip < best_base + best_size);
        int this_covers = (sz && rip < base + sz);

        if (!best) {
            best = s;
            best_base = base;
            best_size = sz;
            continue;
        }
        if (this_covers) {
            if (!best_covers || base >= best_base) {
                best = s;
                best_base = base;
                best_size = sz;
            }
        } else if (!best_covers && base >= best_base) {
            best = s;
            best_base = base;
            best_size = sz;
        }
    }

    if (!best)
        return false;

    if (out_name)
        *out_name = sym_name(best);
    if (out_base)
        *out_base = best_base;
    if (out_size)
        *out_size = best_size;
    return true;
}

// ---- name -> address ----
// Returns 0 if not found.
uintptr_t kernel_symbol_lookup(const char* name) {
    if (!name)
        return 0;

    const elf64_sym_t* tab = syms_view.symtab;
    size_t n = syms_view.sym_count;

    for (size_t i = 0; i < n; ++i) {
        const elf64_sym_t* s = &tab[i];
        if (!sym_is_candidate(s))
            continue;

        const char* sname = sym_name(s);
        if (sname && *sname && streq(sname, name)) {
            return (uintptr_t) s->st_value; // add slide if PIE/KASLR
        }
    }
    return 0;
}

typedef struct [[gnu::packed]] DebugStackFrame {
    struct DebugStackFrame* rbp;
    uint64_t rip;
} DebugStackFrame;

static void log_stack_trace_from(DebugStackFrame* frame) {
    log_raw("Stack Trace:\n");

    for (int depth = 0; frame && frame->rip && depth < 30; depth++) {
        const char* name = NULL;
        uintptr_t base = 0;
        size_t size = 0;

        if (!kernel_find_symbol((uintptr_t) frame->rip, &name, &base, &size) || !name) {
            log_raw("       [UNKNOWN] <%#lx>\n", (unsigned long) frame->rip);
        } else {
            unsigned long off = (unsigned long) (frame->rip - base);
            log_raw("       %s+%lu <%#lx>\n", name, off, (unsigned long) frame->rip);
        }
        frame = frame->rbp;
    }
}

void log_stack_trace() {
    DebugStackFrame* frame;
    asm volatile("movq %%rbp, %0" : "=r"(frame));
    log_stack_trace_from(frame);
}
