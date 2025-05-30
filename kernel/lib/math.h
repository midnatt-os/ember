#pragma once

#define CLAMP(V, LO, HI) (((V) < (LO)) ? (LO) : ((V) > (HI) ? (HI) : (V)))

#define MATH_MIN(A, B) \
    ({                 \
        auto a = (A);  \
        auto b = (B);  \
        a < b ? a : b; \
    })

#define MATH_MAX(A, B) \
    ({                 \
        auto a = (A);  \
        auto b = (B);  \
        a > b ? a : b; \
    })
