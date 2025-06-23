#pragma once

#include <stdint.h>

void acpi_early_init(uintptr_t rsdp_address);
void acpi_finalize_init();
