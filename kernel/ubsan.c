#include <stdint.h>

#include "common/log.h"
#include "common/panic.h"

typedef struct {
    const char *filename;
    uint32_t line;
    uint32_t column;
} SourceLocation;

typedef struct {
    uint16_t kind;
    uint16_t info;
    char name[];
} TypeDescriptor;

typedef struct {
    SourceLocation location;
} DataOnlyLocation;

typedef struct {
    SourceLocation location;
    TypeDescriptor *type;
} DataLocationType;

typedef struct {
    SourceLocation location;
    TypeDescriptor *type;
} DataLoadInvalidValue;

typedef struct {
    SourceLocation location;
    SourceLocation attr_location;
    int arg_index;
} DataNonNullArg;

typedef struct {
    SourceLocation location;
    TypeDescriptor *lhs_type;
    TypeDescriptor *rhs_type;
} DataShiftOutOfBounds;

typedef struct {
    SourceLocation location;
    TypeDescriptor *array_type;
    TypeDescriptor *index_type;
} DataOutOfBounds;

typedef struct {
    SourceLocation location;
    TypeDescriptor *type;
    uint8_t alignment;
    uint8_t type_check_kind;
} DataTypeMismatch;

typedef struct {
    SourceLocation location;
    SourceLocation assumption_location;
    TypeDescriptor *type;
} DataAlignmentAssumption;

typedef struct {
    SourceLocation location;
    TypeDescriptor *from_type;
    TypeDescriptor *to_type;
    uint8_t kind;
} DataImplicitConversion;

typedef struct {
    SourceLocation location;
    uint8_t kind;
} DataInvalidBuiltin;

typedef struct {
    SourceLocation location;
    struct type_descriptor_t *type;
} DataFunctionTypeMismatch;



const char *kind_to_type(uint16_t kind) {
    const char *type;
    switch(kind) {
        case 0:  type = "integer"; break;
        case 1:  type = "float"; break;
        default: type = "unknown"; break;
    }
    return type;
}

unsigned int info_to_bits(uint16_t info) {
    return 1 << (info >> 1);
}

void __ubsan_handle_load_invalid_value(DataLoadInvalidValue *data, uintptr_t value) {
    log(LOG_WARN, "UBSAN", "load_invalid_value @ %s:%u:%u {value: %#lx}", data->location.filename, data->location.line, data->location.column, value);
}

void __ubsan_handle_nonnull_arg(DataNonNullArg *data) {
    log(LOG_WARN, "UBSAN", "handle_nonnull_arg @ %s:%u:%u {arg_index: %i}", data->location.filename, data->location.line, data->location.column, data->arg_index);
}

void __ubsan_handle_nullability_arg(DataNonNullArg *data) {
    log(LOG_WARN, "UBSAN", "nullability_arg @ %s:%u:%u {arg_index: %i}", data->location.filename, data->location.line, data->location.column, data->arg_index);
}

void __ubsan_handle_nonnull_return_v1(DataOnlyLocation *data [[maybe_unused]], SourceLocation *location) {
    log(LOG_WARN, "UBSAN", "nonnull_return @ %s:%u:%u", location->filename, location->line, location->column);
}

void __ubsan_handle_nullability_return_v1(DataOnlyLocation *data [[maybe_unused]], SourceLocation *location) {
    log(LOG_WARN, "UBSAN", "nullability_return @ %s:%u:%u", location->filename, location->line, location->column);
}

void __ubsan_handle_vla_bound_not_positive(DataLocationType *data, uintptr_t bound) {
    log(LOG_WARN,
        "UBSAN",
        "vla_bound_not_positive @ %s:%u:%u {bound: %#lx, type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        bound,
        info_to_bits(data->type->info),
        kind_to_type(data->type->kind),
        data->type->name);
}

void __ubsan_handle_add_overflow(DataLocationType *data, uintptr_t lhs, uintptr_t rhs) {
    log(LOG_WARN,
        "UBSAN",
        "add_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        lhs,
        rhs,
        info_to_bits(data->type->info),
        kind_to_type(data->type->kind),
        data->type->name);
}

void __ubsan_handle_sub_overflow(DataLocationType *data, uintptr_t lhs, uintptr_t rhs) {
    log(LOG_WARN,
        "UBSAN",
        "sub_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        lhs,
        rhs,
        info_to_bits(data->type->info),
        kind_to_type(data->type->kind),
        data->type->name);
}

void __ubsan_handle_mul_overflow(DataLocationType *data, uintptr_t lhs, uintptr_t rhs) {
    log(LOG_WARN,
        "UBSAN",
        "mul_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        lhs,
        rhs,
        info_to_bits(data->type->info),
        kind_to_type(data->type->kind),
        data->type->name);
}

void __ubsan_handle_divrem_overflow(DataLocationType *data, uintptr_t lhs, uintptr_t rhs) {
    log(LOG_WARN,
        "UBSAN",
        "divrem_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        lhs,
        rhs,
        info_to_bits(data->type->info),
        kind_to_type(data->type->kind),
        data->type->name);
}

void __ubsan_handle_negate_overflow(DataLocationType *data, uintptr_t old) {
    log(LOG_WARN, "UBSAN", "negate_overflow @ %s:%u:%u {old: %#lx, type: %u-bit %s %s}", data->location.filename, data->location.line, data->location.column, old, info_to_bits(data->type->info), kind_to_type(data->type->kind), data->type->name
    );
}

void __ubsan_handle_shift_out_of_bounds(DataShiftOutOfBounds *data, uintptr_t lhs, uintptr_t rhs) {
    log(LOG_WARN,
        "UBSAN",
        "shift_out_of_bounds @ %s:%u:%u {lhs: %#lx, rhs: %#lx, rhs_type: %u-bit %s %s, lhs_type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        lhs,
        rhs,
        info_to_bits(data->rhs_type->info),
        kind_to_type(data->rhs_type->kind),
        data->rhs_type->name,
        info_to_bits(data->lhs_type->info),
        kind_to_type(data->lhs_type->kind),
        data->lhs_type->name);
}

void __ubsan_handle_out_of_bounds(DataOutOfBounds *data, uint64_t index) {
    log(LOG_WARN,
        "UBSAN",
        "out_of_bounds @ %s:%u:%u {index: %#lx, array_type: %u-bit %s %s, index_type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        index,
        info_to_bits(data->array_type->info),
        kind_to_type(data->array_type->kind),
        data->array_type->name,
        info_to_bits(data->index_type->info),
        kind_to_type(data->index_type->kind),
        data->index_type->name);
}

void __ubsan_handle_type_mismatch_v1(DataTypeMismatch *data, void *pointer) {
    static const char *kind_strs[] = { "load of",     "store to",  "reference binding to",    "member access within", "member call on",      "constructor call on", "downcast of",
                                       "downcast of", "upcast of", "cast to virtual base of", "nonnull binding to",   "dynamic operation on" };

    if(pointer == nullptr) {
        log(LOG_WARN, "UBSAN", "type_mismatch @ %s:%u:%u (%s nullptr pointer of type %s)", data->location.filename, data->location.line, data->location.column, kind_strs[data->type_check_kind], data->type->name);
    } else if((1 << data->alignment) - 1) {
        log(LOG_WARN, "UBSAN", "type_mismatch @ %s:%u:%u (%s misaligned address %#lx of type %s)", data->location.filename, data->location.line, data->location.column, kind_strs[data->type_check_kind], (uintptr_t) pointer, data->type->name);
    } else {
        log(LOG_WARN,
            "UBSAN",
            "type_mismatch @ %s:%u:%u (%s address %#lx, not enough spce for type %s)",
            data->location.filename,
            data->location.line,
            data->location.column,
            kind_strs[data->type_check_kind],
            (uintptr_t) pointer,
            data->type->name);
    }
}

void __ubsan_handle_alignment_assumption(DataAlignmentAssumption *data, void *, void *, void *) {
    log(LOG_WARN, "UBSAN", "alignment_assumption @ %s:%u:%u", data->location.filename, data->location.line, data->location.column);
}

void __ubsan_handle_implicit_conversion(DataImplicitConversion *data, void *, void *) {
    log(LOG_WARN,
        "UBSAN",
        "implicit_conversion @ %s:%u:%u {from_type: %u-bit %s %s, to_type: %u-bit %s %s}",
        data->location.filename,
        data->location.line,
        data->location.column,
        info_to_bits(data->from_type->info),
        kind_to_type(data->from_type->kind),
        data->from_type->name,
        info_to_bits(data->to_type->info),
        kind_to_type(data->to_type->kind),
        data->to_type->name);
}

void __ubsan_handle_invalid_builtin(DataInvalidBuiltin *data) {
    log(LOG_WARN, "UBSAN", "invalid_builtin @ %s:%u:%u", data->location.filename, data->location.line, data->location.column);
}

void __ubsan_handle_pointer_overflow(DataOnlyLocation *data, void *, void *) {
    log(LOG_WARN, "UBSAN", "pointer_overflow @ %s:%u:%u", data->location.filename, data->location.line, data->location.column);
}

[[noreturn]] void __ubsan_handle_builtin_unreachable(DataOnlyLocation *data) {
    panic("UBSAN", "builtin_unreachable @ %s:%u:%u", data->location.filename, data->location.line, data->location.column);
}

[[noreturn]] void __ubsan_handle_missing_return(DataOnlyLocation *data) {
    panic("UBSAN", "missing_return @ %s:%u:%u", data->location.filename, data->location.line, data->location.column);
}

void __ubsan_handle_function_type_mismatch(DataFunctionTypeMismatch *data, [[maybe_unused]] void *value) {
    log(LOG_WARN, "UBSAN", "function type mismatch @ %s:%u:%u", data->location.filename, data->location.line, data->location.column);
}
