#include "cpu/fpu.h"

#include <cpuid.h>
#include <stddef.h>

#include "common/assert.h"
#include "cpu/registers.h"



uint32_t fpu_area_size = 0;
void (*fpu_save)(void *area) = 0;
void (*fpu_restore)(void *area) = 0;

static inline void xsave(void *area) {
    __asm__ volatile("xsave (%0)" : : "r"(area), "a"(0xFFFF'FFFF), "d"(0xFFFF'FFFF) : "memory");
}

static inline void xrstor(void *area) {
    __asm__ volatile("xrstor (%0)" : : "r"(area), "a"(0xFFFF'FFFF), "d"(0xFFFF'FFFF) : "memory");
}

static inline void fxsave(void *area) {
    __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
}

static inline void fxrstor(void *area) {
    __asm__ volatile("fxrstor (%0)" : : "r"(area) : "memory");
}

void fpu_init(void) {
    uint32_t eax, ebx, ecx, edx;

    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        fpu_area_size = 0;
        fpu_save = NULL;
        fpu_restore = NULL;
        return;
    }

    const bool fxsr_supported = (edx >> 24) & 1;
    const bool sse_supported  = (edx >> 25) & 1;
    if (!fxsr_supported || !sse_supported) {
        fpu_area_size = 0;
        fpu_save = NULL;
        fpu_restore = NULL;
        return;
    }

    const bool xsave_cpu_supported = (ecx >> 26) & 1;
    if (xsave_cpu_supported) {
        uint32_t sub_eax, sub_ebx, sub_ecx, sub_edx;
        if (!__get_cpuid_count(0xD, 0, &sub_eax, &sub_ebx, &sub_ecx, &sub_edx)) {
            fpu_area_size = 512;
        } else {
            fpu_area_size = sub_eax;
            ASSERT(fpu_area_size > 0);
        }
        fpu_save    = xsave;
        fpu_restore = xrstor;
    } else {
        fpu_area_size = 512;
        fpu_save      = fxsave;
        fpu_restore   = fxrstor;
    }
}

void fpu_init_cpu(void) {
    uint32_t eax, ebx, ecx, edx;

    bool fxsr_supported = false;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        fxsr_supported = (edx >> 24) & 1;
    }
    ASSERT(fxsr_supported && "CPU does not support FXSR");

    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2);   // CR0.EM = 0
    cr0 |=  (1ULL << 1);   // CR0.MP = 1
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |=  (1ULL << 9);   // CR4.OSFXSR = 1
    cr4 |=  (1ULL << 10);  // CR4.OSXMMEXCPT = 1
    write_cr4(cr4);

    bool xsave_cpu_supported = false;
    if ((ecx >> 26) & 1) {
        xsave_cpu_supported = true;
    }

    if (xsave_cpu_supported) {
        cr4 = read_cr4();
        cr4 |= (1ULL << 18);   // CR4.OSXSAVE = 1
        write_cr4(cr4);

        uint64_t xcr0 = 0;
        xcr0 |= (1ULL << 0);   // XCR0.X87 state enabled
        xcr0 |= (1ULL << 1);   // XCR0.SSE  state enabled

        bool avx_cpu_supported = false;
        if ((ecx >> 28) & 1) {
            avx_cpu_supported = true;
            xcr0 |= (1ULL << 2);  // XCR0.AVX enabled
        }

        if (avx_cpu_supported) {
            uint32_t leaf7_eax, leaf7_ebx, leaf7_ecx, leaf7_edx;
            if (__get_cpuid_count(7, 0, &leaf7_eax, &leaf7_ebx, &leaf7_ecx, &leaf7_edx)) {
                if ((leaf7_ebx >> 16) & 1) {
                    xcr0 |= (1ULL << 5);
                    xcr0 |= (1ULL << 6);
                    xcr0 |= (1ULL << 7);
                }
            }
        }

        __asm__ volatile (
            "xsetbv"
            :
            : "c"(0), "a"((uint32_t)xcr0), "d"((uint32_t)(xcr0 >> 32))
            : "memory"
        );
    }
}
