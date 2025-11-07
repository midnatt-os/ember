#pragma once

#define DIV_CEIL(DIVIDEND, DIVISOR)           \
    ({                                        \
        auto divisor = (DIVISOR);             \
        ((DIVIDEND) + divisor - 1) / divisor; \
    })

#define ALIGN_UP(VALUE, PRECISION)                \
    ({                                            \
        auto precision = (PRECISION);             \
        DIV_CEIL((VALUE), precision) * precision; \
    })

#define ALIGN_DOWN(VALUE, PRECISION)       \
    ({                                     \
        auto precision = (PRECISION);      \
        ((VALUE) / precision) * precision; \
    })
