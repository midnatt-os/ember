#pragma once

#include "common/limine_requests.h"

#include <stdint.h>

#define HHDM(PTR) ((uintptr_t) (PTR) + hhdm_request.response->offset)
#define PHYS_FROM_HHDM(PTR) (PTR - hhdm_request.response->offset)
