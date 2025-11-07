#pragma once

#include <stddef.h>
#include <stdint.h>

#define CONTAINER_OF(PTR, TYPE, MEMBER)                                                                                               \
    ({                                                                                                                                \
        static_assert(__builtin_types_compatible_p(typeof(((TYPE*) 0)->MEMBER), typeof(*PTR)), "member type does not match pointer"); \
        (TYPE*) (((uintptr_t) (PTR)) - __builtin_offsetof(TYPE, MEMBER));                                                             \
    })
