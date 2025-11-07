#include "dev/hpet.h"

#include "common/assert.h"
#include "common/log.h"
#include "mem/mmio.h"

#include <uacpi/acpi.h>
#include <uacpi/tables.h>


enum {
    CAP_ID = 0x000,
    GEN_CONFIG = 0x010,
    GEN_INTSTAT = 0x020,
    MAIN_COUNTER = 0x0F0,
};

static void* hpet = nullptr;
static uint64_t period = 0;

static uint64_t read(uint64_t reg) {
    return mmio_read64(hpet + reg);
}

static void write(uint64_t reg, uint64_t val) {
    mmio_write64(hpet + reg, val);
}

uint64_t hpet_time() {
    return read(MAIN_COUNTER) * period;
}

void hpet_init() {
    uacpi_table hpet_table;
    ASSERT(uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &hpet_table) == UACPI_STATUS_OK);

    hpet = mmio_map(((struct acpi_hpet*) hpet_table.ptr)->address.address, 1024);

    period = (read(CAP_ID) >> 32) / 1'000'000;

    uint64_t cfg = read(GEN_CONFIG);
    cfg &= ~1ULL;
    write(GEN_CONFIG, cfg);

    write(MAIN_COUNTER, 0);

    cfg |= 1ULL;
    write(GEN_CONFIG, cfg);

    logln(LOG_INFO, "HPET", "Initialized");
}
