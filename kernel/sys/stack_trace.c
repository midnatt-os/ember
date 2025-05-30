#include "sys/stack_trace.h"

#include "common/assert.h"
#include "common/log.h"
#include "common/modules.h"
#include "lib/string.h"

#define SYMF_MAGIC 0x464D5953

typedef struct [[gnu::packed]] {
    uint32_t magic;
    uint32_t symbol_count;
} Header;

typedef struct [[gnu::packed]] {
    uint64_t address;
    char type;
    uint64_t name_offset;
} Entry;



Header* header;

void load_kernel_symbols(Modules* modules) {
    Module* sym_module = find_module(modules, "ember_symbols");
    ASSERT(sym_module != nullptr && ((Header*) sym_module->address)->magic == SYMF_MAGIC);
    header = (Header*) sym_module->address;
}

static bool find_symbol(uintptr_t rip, Entry *out_sym) {
    if (!header) return false;

    Entry* entries = (Entry*) (header + 1);

    Entry* best = nullptr;
    for (uint32_t i = 0; i < header->symbol_count; i++) {
        if (entries[i].address > rip)
            break;

        best = &entries[i];
    }

    if (!best)
        return false;

    *out_sym = (Entry) {
        .address = best->address,
        .type = best->type,
        .name_offset = best->name_offset
    };

    return true;
}

static inline char *get_symbol_name(uint64_t name_offset) {
    char* base = (char*) (((Entry*) (header + 1)) + header->symbol_count); // I'm sorry
    return base + name_offset;
}

typedef struct [[gnu::packed]] DebugStackFrame {
    struct DebugStackFrame *rbp;
    uint64_t rip;
} DebugStackFrame;

static void log_stack_trace_from(DebugStackFrame* frame) {
    logln(LOG_DEBUG, "Stack Trace", "");

    for (int depth = 0; frame && frame->rip && depth < 30; depth++) {
        Entry sym;

        if (!find_symbol(frame->rip, &sym))
            log_raw("       [UNKNOWN] <%#lx>\n", frame->rip);
        else
            log_raw("       %s+%lu <%#lx>\n", get_symbol_name(sym.name_offset), frame->rip - sym.address, frame->rip);

        frame = frame->rbp;
    }
}

void log_stack_trace() {
    DebugStackFrame *frame;
    asm volatile("movq %%rbp, %0" : "=r"(frame));
    log_stack_trace_from(frame);
}
