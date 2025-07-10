#include "dev/keyboard.h"
#include "drivers/ps2kb.h"
#include "fs/devfs/devfs.h"
#include "memory/heap.h"
#include "sched/sched.h"



ssize_t ps2kb_read([[maybe_unused]] void* ctx, [[maybe_unused]] void* buf, [[maybe_unused]] size_t len, off_t _) {
    PS2Keyboard* dev = (PS2Keyboard*) ctx;

    if (len < sizeof(KeyEvent)) return 0;

    size_t count = 0;
    KeyEvent* out = (KeyEvent*) buf;
    [[maybe_unused]] Thread* current = sched_get_current_thread();

    while (count < len / sizeof(KeyEvent)) {
        if (ringbuf_empty(dev->ke_buffer)) {
            if (count > 0)
                break; // Return what we've read so far

            // Block and wait
            /*dev->waiting_thread = current;
            sched_yield(STATUS_BLOCKED);
            dev->waiting_thread = nullptr;*/

            // Still nothing? Exit with 0.
            if (ringbuf_empty(dev->ke_buffer))
                return 0;
        }

        uint64_t e;
        if (!ringbuf_pop(dev->ke_buffer, &e))
            break;

        KeyEvent* event = (KeyEvent*) e;

        out[count++] = *event;
        kfree(event);
    }

    return count * sizeof(KeyEvent);
}

DeviceOps kb_dev_ops = { .read = ps2kb_read };
