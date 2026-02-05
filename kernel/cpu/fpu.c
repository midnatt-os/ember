#include "common/align.h"
#include "common/assert.h"
#include "common/log.h"
#include "lib/mem.h"
#include "sched/thread.h"

#include <cpuid.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* XCR0 bits */
#define XCR0_X87 (1ull << 0)
#define XCR0_SSE (1ull << 1)
#define XCR0_AVX (1ull << 2)
#define XCR0_BNDREG (1ull << 3)
#define XCR0_BNDCSR (1ull << 4)
#define XCR0_OPMASK (1ull << 5)
#define XCR0_ZMM_HI256 (1ull << 6)
#define XCR0_HI16_ZMM (1ull << 7)

uint32_t g_fpu_area_size = 512;
void (*fpu_save)(void* area) = nullptr;
void (*fpu_restore)(void* area) = nullptr;

static bool g_fpu_use_xsave = false;
static uint64_t g_xcr0_mask = 0;

static inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val) : : "memory");
    return val;
}

static inline void write_cr0(uint64_t val) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

static inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val) : : "memory");
    return val;
}

static inline void write_cr4(uint64_t val) {
    __asm__ volatile("mov %0, %%cr4" : : "r"(val) : "memory");
}

static inline void xsetbv(uint32_t idx, uint64_t val) {
    uint32_t eax = (uint32_t) val;
    uint32_t edx = (uint32_t) (val >> 32);
    __asm__ volatile("xsetbv" : : "a"(eax), "d"(edx), "c"(idx) : "memory");
}

static void xsave_wrapper(void* area) {
    uint32_t eax = (uint32_t) g_xcr0_mask;
    uint32_t edx = (uint32_t) (g_xcr0_mask >> 32);
    __asm__ volatile("xsave64 (%0)" : : "r"(area), "a"(eax), "d"(edx) : "memory");
}

static void xrstor_wrapper(void* area) {
    uint32_t eax = (uint32_t) g_xcr0_mask;
    uint32_t edx = (uint32_t) (g_xcr0_mask >> 32);
    __asm__ volatile("xrstor64 (%0)" : : "r"(area), "a"(eax), "d"(edx) : "memory");
}

static void fxsave_wrapper(void* area) {
    __asm__ volatile("fxsave64 (%0)" : : "r"(area) : "memory");
}

static void fxrstor_wrapper(void* area) {
    __asm__ volatile("fxrstor64 (%0)" : : "r"(area) : "memory");
}

size_t fpu_required_alignment(void) {
    return g_fpu_use_xsave ? 64u : 16u;
}

bool fpu_using_xsave(void) {
    return g_fpu_use_xsave;
}

uint64_t fpu_xcr0_mask(void) {
    return g_xcr0_mask;
}

void fpu_init_features(void) {
    uint32_t eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);

    // Hard requirement: FXSR must exist on x86_64
    ASSERT((edx & (1u << 24)) && "FXSR not supported");

    bool cpu_has_xsave = (ecx & (1u << 26)) != 0;
    bool cpu_has_avx = (ecx & (1u << 28)) != 0;

    g_fpu_use_xsave = false;
    g_xcr0_mask = 0;

    if (cpu_has_xsave) {
        // Temporarily enable OSXSAVE on BSP to probe XCR0 capabilities
        uint64_t cr4 = read_cr4();
        write_cr4(cr4 | (1ull << 18));

        // Get supported XCR0 bits
        uint32_t d0_eax, d0_ebx, d0_ecx, d0_edx;
        __cpuid_count(0xD, 0, d0_eax, d0_ebx, d0_ecx, d0_edx);
        uint64_t xcr0_supported = ((uint64_t) d0_edx << 32) | d0_eax;

        // Calculate Desired Mask
        uint64_t xcr0_desired = XCR0_X87 | XCR0_SSE;
        if (cpu_has_avx)
            xcr0_desired |= XCR0_AVX;

        // ... [Insert AVX512 logic here if needed] ...

        g_xcr0_mask = xcr0_desired & xcr0_supported;

        // Calculate required save area size
        __cpuid_count(0xD, 0, d0_eax, d0_ebx, d0_ecx, d0_edx);
        uint32_t needed = d0_eax;
        g_fpu_area_size = (uint32_t) ALIGN_UP((needed < 512 ? 512 : needed), 64);

        g_fpu_use_xsave = true;
        fpu_save = xsave_wrapper;
        fpu_restore = xrstor_wrapper;
    } else {
        g_fpu_area_size = 512;
        fpu_save = fxsave_wrapper;
        fpu_restore = fxrstor_wrapper;
    }
}

void fpu_init_core(void) {
    // 1. CR0: Enable Monitoring / Native Exceptions
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ull << 2); // EM = 0
    cr0 |= (1ull << 1); // MP = 1
    cr0 |= (1ull << 5); // NE = 1
    write_cr0(cr0);

    // 2. CR4: Enable SSE / FXSR
    uint64_t cr4 = read_cr4();
    cr4 |= (1ull << 9); // OSFXSR
    cr4 |= (1ull << 10); // OSXMMEXCPT

    // 3. CR4: Enable XSAVE (if supported globally)
    if (g_fpu_use_xsave) {
        cr4 |= (1ull << 18); // OSXSAVE
    }
    write_cr4(cr4);

    // 4. XCR0: Set feature mask (if using XSAVE)
    if (g_fpu_use_xsave) {
        xsetbv(0, g_xcr0_mask);
    }

    // Optional: Re-init x87/SSE state for this core
    __asm__ volatile("fninit");
}

void fpu_init_thread_state(void* area) {
    ASSERT(area != nullptr);
    ASSERT(fpu_save != nullptr);

    /* Reset x87 state */
    __asm__ volatile("fninit");

    /* Reset MXCSR to default (0x1F80) */
    uint32_t mxcsr = 0x1F80u;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));

    /* Capture into the thread buffer */
    fpu_save(area);
}
