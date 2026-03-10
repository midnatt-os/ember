#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    INIT_STAGE_BOOT,
    INIT_STAGE_EARLY,
    INIT_STAGE_BEFORE_MAIN,
    INIT_STAGE_MAIN,
    INIT_STAGE_BEFORE_DEV,
    INIT_STAGE_DEV,
    INIT_STAGE_LATE,
} init_stage_t;

typedef enum {
    INIT_SCOPE_BSP,
    INIT_SCOPE_APS,
    INIT_SCOPE_ALL,
} init_scope_t;

typedef struct {
    const char* name;
    init_stage_t stage;
    init_scope_t scope;
    void (*fn)(void);
    const char* const* dependencies;
    size_t dependency_count;
    bool completed;
} init_target_t;

extern init_stage_t g_init_stage_current;
extern uint8_t init_targets_start[];
extern uint8_t init_targets_end[];

#define INIT_TARGETS ((init_target_t*) (void*) init_targets_start)

#define INIT_TARGET_COUNT ((size_t) (((uintptr_t) init_targets_end - (uintptr_t) init_targets_start) / sizeof(init_target_t)))

void init_reset_ap(void);
void init_run_stage(init_stage_t stage, bool is_ap);

#define INIT_CONCAT_IMPL(A, B) A##B
#define INIT_CONCAT(A, B) INIT_CONCAT_IMPL(A, B)

#define INIT_DEPS(...) (__VA_ARGS__)
#define INIT_DEPS_UNWRAP(...) {__VA_ARGS__}

#define INIT_TARGET(NAME, STAGE, SCOPE, DEPS) INIT_TARGET_IMPL(__COUNTER__, NAME, STAGE, SCOPE, DEPS)

#define INIT_TARGET_IMPL(ID, NAME, STAGE, SCOPE, DEPS)                                                                  \
    static void INIT_CONCAT(init_target_fn_, ID)(void);                                                                 \
    static const char* const INIT_CONCAT(init_target_deps_, ID)[] = INIT_DEPS_UNWRAP DEPS;                              \
    static init_target_t INIT_CONCAT(init_target_desc_, ID) __attribute__((used, section("init_targets"))) = {          \
        .name = #NAME,                                                                                                  \
        .stage = (STAGE),                                                                                               \
        .scope = (SCOPE),                                                                                               \
        .fn = INIT_CONCAT(init_target_fn_, ID),                                                                         \
        .dependencies = INIT_CONCAT(init_target_deps_, ID),                                                             \
        .dependency_count = sizeof(INIT_CONCAT(init_target_deps_, ID)) / sizeof(INIT_CONCAT(init_target_deps_, ID)[0]), \
        .completed = false,                                                                                             \
    };                                                                                                                  \
    static void INIT_CONCAT(init_target_fn_, ID)(void)
