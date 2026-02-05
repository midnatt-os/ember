#include "drivers/tty.h"

#include "common/errno.h"
#include "common/log.h"
#include "fs/impl/devfs.h"


static ssize_t tty_read(void* buffer, size_t count, off_t offset) {
    (void) buffer;
    (void) count;
    (void) offset;

    // TODO: Implement Keyboard Input Ring Buffer here
    return 0;
}

static ssize_t tty_write(const void* buffer, size_t count, off_t offset) {
    (void) offset; // TTY is a stream, offset is ignored

    if (!buffer)
        return -EINVAL;
    if (count == 0)
        return 0;

    log_console_write((const char*) buffer, count);

    return count;
}

// Define the operations for the TTY device
static dev_ops_t tty_ops = { .read = tty_read, .write = tty_write };

void tty_init() {
    // 1. Create the primary TTY device
    // Major 4, Minor 0 are standard Unix numbers for tty0
    devfs_make_node("tty0", V_CHR, 4, 0, &tty_ops);

    // 2. Create 'console' alias (often points to the active TTY)
    devfs_make_node("console", V_CHR, 5, 1, &tty_ops);

    logln(LOG_INFO, "TTY", "Initialized /dev/tty0 and /dev/console");
}
