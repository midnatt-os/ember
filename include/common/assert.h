#pragma once

#include "common/panic.h"

#define ASSERT(cond) \
    do { \
        if (!(cond)) \
            panic("Assertion: %s failed at %s:%d\n", #cond, __FILE__, __LINE__); \
    } while (0)
