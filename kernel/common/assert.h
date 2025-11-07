#pragma once

#include "common/panic.h"

#define ASSERT(cond) ({ if (!(cond)) panic("Assertion: %s failed at %s:%d\n", #cond, __FILE__, __LINE__); })

#define ASSERT_UNREACHABLE() ({ panic("Unreachable %s:%d\n", __FILE__, __LINE__); __builtin_unreachable(); })
