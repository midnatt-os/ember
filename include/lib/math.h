#pragma once

#define CLAMP(V, LO, HI) (((V) < (LO)) ? (LO) : ((V) > (HI) ? (HI) : (V)))

static inline int math_min(int a, int b) {
    return a < b ? a : b;
}

static inline int math_max(int a, int b) {
    return a > b ? a : b;
}
