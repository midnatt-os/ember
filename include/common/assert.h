#pragma once

#include "common/panic.h"

// TODO: change to one line (and remove the do-while)
#define ASSERT(cond) \
    do { \
        if (!(cond)) \
            panic("Assertion: %s failed at %s:%d\n", #cond, __FILE__, __LINE__); \
    } while (0)
