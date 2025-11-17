#include "common/limine_requests.h"
#include "common/lock/spinlock.h"
#include "common/log.h"
#include "cpu/tsc.h"
#include "mem/heap.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "sys/time.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct xoshiro256pp_state {
    uint64_t s[4];
};

typedef struct {
    int64_t raw_value;
} fixed48_16_t;

struct firework_data {
    uint32_t x;
    uint32_t y;

    fixed48_16_t ax;
    fixed48_16_t ay;
    fixed48_16_t vx;
    fixed48_16_t vy;

    uint32_t color;
    fixed48_16_t range;
    uint64_t rngseed;
};

struct firework_job {
    struct firework_job* next;
    void (*func)(void* arg);
    void* arg;
};

static struct limine_framebuffer* fb;
static uint8_t* fb_pixels;
static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_pitch;
static uint32_t fb_bytes_per_pixel;
static uint8_t fb_r_shift;
static uint8_t fb_r_size;
static uint8_t fb_g_shift;
static uint8_t fb_g_size;
static uint8_t fb_b_shift;
static uint8_t fb_b_size;

static spinlock_t job_lock = SPINLOCK_NEW;
static struct firework_job* job_stack;
static uint32_t flush_counter;

extern void thread_exit(void);

static inline uint64_t rol64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256pp(struct xoshiro256pp_state* state) {
    uint64_t result = rol64(state->s[0] + state->s[3], 23) + state->s[0];
    uint64_t t = state->s[1] << 17;

    state->s[2] ^= state->s[0];
    state->s[3] ^= state->s[1];
    state->s[1] ^= state->s[2];
    state->s[0] ^= state->s[3];

    state->s[2] ^= t;
    state->s[3] = rol64(state->s[3], 45);

    return result;
}

static void seed_rng(struct xoshiro256pp_state* state, uint64_t seed) {
    for (int i = 0; i < 4; i++) {
        state->s[i] = seed;
        seed *= 0xdeadbeef87654321ULL;
    }
}

static inline fixed48_16_t fixed_from_int(int32_t value) {
    fixed48_16_t v;
    v.raw_value = (int64_t) value * 65536;
    return v;
}

static inline fixed48_16_t fixed_from_raw(int64_t raw) {
    fixed48_16_t v;
    v.raw_value = raw;
    return v;
}

static inline int32_t fixed_to_int(fixed48_16_t value) {
    return (int32_t) (value.raw_value / 65536);
}

static inline fixed48_16_t fixed_add(fixed48_16_t a, fixed48_16_t b) {
    return (fixed48_16_t) { .raw_value = a.raw_value + b.raw_value };
}

static inline fixed48_16_t fixed_sub(fixed48_16_t a, fixed48_16_t b) {
    return (fixed48_16_t) { .raw_value = a.raw_value - b.raw_value };
}

static inline fixed48_16_t fixed_mul(fixed48_16_t a, fixed48_16_t b) {
    return (fixed48_16_t) { .raw_value = (a.raw_value * b.raw_value) / 65536 };
}

static inline fixed48_16_t fixed_neg(fixed48_16_t value) {
    return (fixed48_16_t) { .raw_value = -value.raw_value };
}

static inline fixed48_16_t rand_fixed48_16(struct xoshiro256pp_state* rng) {
    return fixed_from_raw((int64_t) (xoshiro256pp(rng) % 65536));
}

static inline fixed48_16_t rand_fixed48_16_biased(struct xoshiro256pp_state* rng) {
    int64_t a = (int64_t) (xoshiro256pp(rng) % 65536);
    int64_t b = (int64_t) (xoshiro256pp(rng) % 65536);
    if (a < b)
        a = b;

    return fixed_from_raw(a);
}

static inline fixed48_16_t rand_fixed48_16_sign(struct xoshiro256pp_state* rng) {
    fixed48_16_t value = rand_fixed48_16(rng);
    if (xoshiro256pp(rng) & 1)
        return value;

    return fixed_neg(value);
}

static int64_t cos_p(int64_t x) {
    bool negative = false;
    if (x > 102943) {
        x = 205886 - x;
        negative = true;
    }

    int64_t x2 = (x * x) / 131072;
    int64_t x4 = (x2 * x2) / 393216;
    int64_t v = 65536 - x2 + x4;
    if (negative)
        v = -v;

    return v;
}

static fixed48_16_t fixed_cos(fixed48_16_t value) {
    int64_t x = value.raw_value;
    if (x < 0)
        x = -x;

    x %= 411772;
    bool negative = false;
    if (x > 205887) {
        x -= 205887;
        negative = true;
    }

    fixed48_16_t ret = fixed_from_raw(cos_p(x));
    if (negative)
        ret = fixed_neg(ret);

    return ret;
}

static fixed48_16_t fixed_sin(fixed48_16_t value) {
    fixed48_16_t offset = fixed_from_raw(308831);
    return fixed_cos(fixed_add(value, offset));
}

static inline uint32_t scale_component(uint32_t value, uint8_t size, uint8_t shift) {
    if (size == 0)
        return 0;

    uint32_t max_value;
    if (size >= 32)
        max_value = 0xffffffffu;
    else
        max_value = (1u << size) - 1u;

    uint32_t scaled = (value * max_value) / 255u;
    return scaled << shift;
}

static inline uint32_t fb_make_color(uint32_t r, uint32_t g, uint32_t b) {
    return scale_component(r, fb_r_size, fb_r_shift) | scale_component(g, fb_g_size, fb_g_shift) | scale_component(b, fb_b_size, fb_b_shift);
}

static void plot(int32_t x, int32_t y, uint32_t color, bool flush) {
    if (!fb_pixels)
        return;

    if (x < 0 || y < 0)
        return;

    uint32_t ux = (uint32_t) x;
    uint32_t uy = (uint32_t) y;
    if (ux >= fb_width || uy >= fb_height)
        return;

    uint8_t* pixel = fb_pixels + (size_t) uy * fb_pitch + (size_t) ux * fb_bytes_per_pixel;

    if (fb_bytes_per_pixel == 4) {
        *(uint32_t*) pixel = color;
    } else if (fb_bytes_per_pixel == 3) {
        pixel[0] = (uint8_t) color;
        pixel[1] = (uint8_t) (color >> 8);
        pixel[2] = (uint8_t) (color >> 16);
    }

    if (flush) {
        if ((__atomic_fetch_add(&flush_counter, 1u, __ATOMIC_RELAXED) & 0x7f) != 0)
            return;
    }
}

static void fwork_sleep(uint64_t ns) {
    sched_sleep(ns);
}

static const fixed48_16_t dt = { .raw_value = 1048 };

static struct firework_job* pop_job(void) {
    bool prev = spinlock_lock(&job_lock);
    struct firework_job* job = job_stack;
    if (job)
        job_stack = job->next;
    spinlock_unlock(&job_lock, prev);
    return job;
}

static void push_job(struct firework_job* job) {
    bool prev = spinlock_lock(&job_lock);
    job->next = job_stack;
    job_stack = job;
    spinlock_unlock(&job_lock, prev);
}

static void firework_thread_entry(void) {
    struct firework_job* job = pop_job();
    if (!job) {
        logln(LOG_ERROR, "fireworks", "Thread started without work");
        thread_exit();
    }

    void (*fn)(void*) = job->func;
    void* arg = job->arg;
    heap_free(job, sizeof(*job));

    fn(arg);
    thread_exit();
}

static bool firework_spawn_thread(const char* name, void (*func)(void*), void* arg) {
    struct firework_job* job = heap_alloc(sizeof(*job));
    if (!job)
        return false;

    job->func = func;
    job->arg = arg;
    job->next = NULL;

    thread_t* thread = thread_create_kernel((char*) name, firework_thread_entry);
    if (!thread) {
        heap_free(job, sizeof(*job));
        return false;
    }

    push_job(job);
    sched_schedule_thread(thread);
    return true;
}

static uint32_t random_color(struct xoshiro256pp_state* state) {
    uint32_t r = 128 + (xoshiro256pp(state) % 128);
    uint32_t g = 128 + (xoshiro256pp(state) % 128);
    uint32_t b = 128 + (xoshiro256pp(state) % 128);

    return fb_make_color(r, g, b);
}

static void particle_thread(void* arg) {
    struct firework_data data = *(struct firework_data*) arg;
    heap_free(arg, sizeof(struct firework_data));

    struct xoshiro256pp_state rng;
    seed_rng(&rng, data.rngseed);

    fixed48_16_t range = fixed_mul(data.range, rand_fixed48_16_biased(&rng));

    fixed48_16_t angle = fixed_from_raw((int64_t) (xoshiro256pp(&rng) % 411774));
    data.vx = fixed_mul(fixed_cos(angle), range);
    data.vy = fixed_mul(fixed_sin(angle), range);

    data.color = random_color(&rng);
    int expire_in = 2000 + (int) (xoshiro256pp(&rng) % 1000);

    for (int i = 0; i < expire_in;) {
        plot((int32_t) data.x, (int32_t) data.y, data.color, true);
        fwork_sleep(16666000ULL);
        i += 16;

        plot((int32_t) data.x, (int32_t) data.y, 0, false);
        data.ax = fixed_add(data.ax, fixed_mul(data.vx, dt));
        data.ay = fixed_add(data.ay, fixed_mul(data.vy, dt));

        data.x = (uint32_t) fixed_to_int(data.ax);
        data.y = (uint32_t) fixed_to_int(data.ay);

        data.vy = fixed_add(data.vy, fixed_mul(fixed_from_int(10), dt));
    }
}

static bool spawn_particle(struct firework_data* data) {
    struct firework_data* pdata = heap_alloc(sizeof(*pdata));
    if (!pdata)
        return false;

    *pdata = *data;

    if (!firework_spawn_thread("fwork-particle", particle_thread, pdata)) {
        heap_free(pdata, sizeof(*pdata));
        return false;
    }

    return true;
}

static void explode_thread(void* arg) {
    uint64_t seed = (uint64_t) (uintptr_t) arg;

    struct xoshiro256pp_state rng;
    seed_rng(&rng, seed);

    struct firework_data data = { 0 };

    int32_t offset = (int32_t) ((fb_width * 400u) / 1024u);

    data.x = fb_width / 2;
    data.y = fb_height - 1;
    data.ax = fixed_from_int((int32_t) data.x);
    data.ay = fixed_from_int((int32_t) data.y);
    data.vx = fixed_mul(fixed_from_int(offset), rand_fixed48_16_sign(&rng));
    data.vy = fixed_from_int(-400 - (int32_t) (xoshiro256pp(&rng) % 400));
    data.color = random_color(&rng);
    data.range = fixed_from_int(100 + (int32_t) (xoshiro256pp(&rng) % 100));

    int expire_in = 500 + (int) (xoshiro256pp(&rng) % 500);
    for (int i = 0; i < expire_in;) {
        plot((int32_t) data.x, (int32_t) data.y, data.color, true);
        fwork_sleep(16666000ULL);
        i += 16;

        plot((int32_t) data.x, (int32_t) data.y, 0, false);

        data.ax = fixed_add(data.ax, fixed_mul(data.vx, dt));
        data.ay = fixed_add(data.ay, fixed_mul(data.vy, dt));

        data.x = (uint32_t) fixed_to_int(data.ax);
        data.y = (uint32_t) fixed_to_int(data.ay);

        data.vy = fixed_add(data.vy, fixed_mul(fixed_from_int(10), dt));
    }

    int particle_count = 100 + (int) (xoshiro256pp(&rng) % 100);
    for (int i = 0; i < particle_count; i++) {
        struct firework_data pdata = data;
        pdata.rngseed = xoshiro256pp(&rng);
        if (!spawn_particle(&pdata))
            break;
    }
}

static void spawn_explosion(uint64_t seed) {
    if (!firework_spawn_thread("fwork-explosion", explode_thread, (void*) (uintptr_t) seed))
        logln(LOG_WARN, "fireworks", "Failed to spawn explosion thread");
}

static void spawner_thread(void* arg) {
    (void) arg;

    struct xoshiro256pp_state rng;
    seed_rng(&rng, tsc_time());

    while (true) {
        int spawn_count = (int) (xoshiro256pp(&rng) % 5) + 1;
        for (int i = 0; i < spawn_count; i++)
            spawn_explosion(xoshiro256pp(&rng));

        fwork_sleep(2000000000ULL + (xoshiro256pp(&rng) % 2000000000ULL));
    }
}

void init(void) {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count == 0) {
        logln(LOG_WARN, "fireworks", "No framebuffer available; skipping test");
        return;
    }

    fb = framebuffer_request.response->framebuffers[0];
    if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB) {
        logln(LOG_WARN, "fireworks", "Unsupported framebuffer configuration");
        return;
    }

    if (fb->bpp < 24 || (fb->bpp % 8) != 0) {
        logln(LOG_WARN, "fireworks", "Unsupported framebuffer format (%u bpp)", fb->bpp);
        return;
    }

    fb_pixels = (uint8_t*) fb->address;
    fb_width = (uint32_t) fb->width;
    fb_height = (uint32_t) fb->height;
    fb_pitch = (uint32_t) fb->pitch;
    fb_bytes_per_pixel = (uint32_t) (fb->bpp / 8);
    if (fb_bytes_per_pixel < 3) {
        logln(LOG_WARN, "fireworks", "Unsupported bytes per pixel: %u", fb_bytes_per_pixel);
        fb_pixels = NULL;
        return;
    }
    fb_r_shift = fb->red_mask_shift;
    fb_r_size = fb->red_mask_size;
    fb_g_shift = fb->green_mask_shift;
    fb_g_size = fb->green_mask_size;
    fb_b_shift = fb->blue_mask_shift;
    fb_b_size = fb->blue_mask_size;

    logln(LOG_INFO, "fireworks", "Running fireworks test on %ux%u framebuffer", fb_width, fb_height);

    if (!firework_spawn_thread("fwork-spawner", spawner_thread, NULL))
        logln(LOG_ERROR, "fireworks", "Failed to start spawner thread");
}

void deinit(void) {
    logln(LOG_WARN, "fireworks", "Fireworks module unload is not implemented");
}
