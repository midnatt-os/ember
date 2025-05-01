#include "dev/hpet.h"

#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include "common/assert.h"
#include "memory/hhdm.h"
#include "memory/vm.h"
#include "sys/time.h"

typedef struct {
    uint64_t capabilities;
    uint64_t _pad0;
    uint64_t configuration;
    uint64_t _pad1;
    uint64_t interrupt_status;
    uint64_t _pad2[0x19];
    uint64_t main_counter;
    uint64_t _pad3;
} HpetRegisters;



extern VmAddressSpace kernel_as;

static volatile HpetRegisters* hpet_registers;
uint64_t period;

static uint64_t hpet_current() {
    return hpet_registers->main_counter * period;
}

TimeSource hpet_time_source = {
    .name = "HPET",
    .current = hpet_current
};

void hpet_init(void)
{
    uacpi_table hpet_table;
    ASSERT(uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &hpet_table)==UACPI_STATUS_OK);

    struct acpi_hpet* hpet = hpet_table.ptr;
    hpet_registers = (volatile HpetRegisters*) vm_map_direct(&kernel_as, nullptr, PAGE_SIZE, hpet->address.address, VM_PROT_RW, VM_CACHING_UNCACHED, VM_FLAG_NONE);

    period = (hpet_registers->capabilities >> 32) / 1'000'000;

    hpet_registers->configuration &= ~1ULL;
    hpet_registers->main_counter = 0;
    hpet_registers->configuration |= 1ULL;

    uacpi_table_unref(&hpet_table);
}
