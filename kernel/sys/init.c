#include "sys/init.h"

#include "common/assert.h"
#include "common/log.h"
#include "cpu/cpu.h"
#include "lib/string.h"

#define DO_TARGET(TARGET, IS_AP) ((TARGET)->scope == INIT_SCOPE_ALL || ((IS_AP) && (TARGET)->scope == INIT_SCOPE_APS) || (!(IS_AP) && (TARGET)->scope == INIT_SCOPE_BSP))

extern cpu_t* cpus;

init_stage_t g_init_stage_current = INIT_STAGE_BOOT;

static const char* stage_stringify(init_stage_t stage) {
    switch (stage) {
        case INIT_STAGE_BOOT:        return "boot";
        case INIT_STAGE_EARLY:       return "early";
        case INIT_STAGE_BEFORE_MAIN: return "before_main";
        case INIT_STAGE_MAIN:        return "main";
        case INIT_STAGE_BEFORE_DEV:  return "before_dev";
        case INIT_STAGE_DEV:         return "dev";
        case INIT_STAGE_LATE:        return "late";
    }

    ASSERT_UNREACHABLE();
}

static init_target_t* find_target(init_stage_t stage, const char* name) {
    for (size_t i = 0; i < INIT_TARGET_COUNT; i++) {
        init_target_t* target = &INIT_TARGETS[i];

        if (target->stage != stage)
            continue;
        if (!streq(target->name, name))
            continue;

        return target;
    }

    return NULL;
}

static bool dependencies_satisfied(const init_target_t* target, bool is_ap) {
    for (size_t i = 0; i < target->dependency_count; i++) {
        const char* dep_name = target->dependencies[i];
        init_target_t* dep = find_target(target->stage, dep_name);

        if (dep == NULL)
            return false;
        if (!DO_TARGET(dep, is_ap))
            return false;
        if (!dep->completed)
            return false;
    }

    return true;
}

static bool run_stage_pass(init_stage_t stage, bool is_ap) {
    bool progress = false;

    for (size_t i = 0; i < INIT_TARGET_COUNT; i++) {
        init_target_t* target = &INIT_TARGETS[i];

        if (target->stage != stage)
            continue;
        if (!DO_TARGET(target, is_ap))
            continue;
        if (target->completed)
            continue;
        if (!dependencies_satisfied(target, is_ap))
            continue;

        target->completed = true;

        logln(LOG_INFO, "INIT", "Target `%s/%s` (core %lu)", stage_stringify(target->stage), target->name, is_ap ? CPU_CURRENT->seq_id : 0);

        target->fn();
        progress = true;
    }

    return progress;
}

static void diagnose_unresolved_targets(init_stage_t stage, bool is_ap) {
    for (size_t i = 0; i < INIT_TARGET_COUNT; i++) {
        init_target_t* target = &INIT_TARGETS[i];

        if (target->stage != stage)
            continue;
        if (!DO_TARGET(target, is_ap))
            continue;
        if (target->completed)
            continue;

        logln(LOG_WARN, "INIT", "Target `%s/%s` could not run", stage_stringify(target->stage), target->name);

        for (size_t j = 0; j < target->dependency_count; j++) {
            const char* dep_name = target->dependencies[j];
            init_target_t* dep = find_target(target->stage, dep_name);

            if (dep == NULL) {
                logln(LOG_WARN, "INIT", "  missing dependency `%s`", dep_name);
                continue;
            }

            if (!DO_TARGET(dep, is_ap)) {
                logln(LOG_WARN, "INIT", "  dependency `%s/%s` is not runnable on this core", stage_stringify(dep->stage), dep->name);
                continue;
            }

            if (!dep->completed) {
                logln(LOG_WARN, "INIT", "  dependency `%s/%s` is not completed", stage_stringify(dep->stage), dep->name);
                continue;
            }
        }
    }
}

void init_reset_ap(void) {
    g_init_stage_current = INIT_STAGE_BOOT;

    for (size_t i = 0; i < INIT_TARGET_COUNT; i++) {
        init_target_t* target = &INIT_TARGETS[i];

        if (!DO_TARGET(target, true))
            continue;

        target->completed = false;
    }
}

void init_run_stage(init_stage_t stage, bool is_ap) {
    ASSERT(stage >= g_init_stage_current);
    g_init_stage_current = stage;

    while (run_stage_pass(stage, is_ap)) {
    }

    size_t unresolved = 0;

    for (size_t i = 0; i < INIT_TARGET_COUNT; i++) {
        init_target_t* target = &INIT_TARGETS[i];

        if (target->stage != stage)
            continue;
        if (!DO_TARGET(target, is_ap))
            continue;
        if (target->completed)
            continue;

        unresolved++;
    }

    if (unresolved != 0) {
        logln(LOG_WARN, "INIT", "Stage `%s` stalled with %lu unresolved target(s)", stage_stringify(stage), (unsigned long) unresolved);

        diagnose_unresolved_targets(stage, is_ap);
    }

    ASSERT(unresolved == 0);
}
