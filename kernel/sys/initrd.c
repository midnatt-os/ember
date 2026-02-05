#include "sys/initrd.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/log.h"
#include "fs/impl/tmpfs.h"
#include "fs/vfs.h"

#include <lib/string.h>
#include <stdint.h>

#define CPIO_MAGIC_OK(magic) ((magic)[0] == '0' && (magic)[1] == '7' && (magic)[2] == '0' && (magic)[3] == '7' && (magic)[4] == '0' && (magic)[5] == '1')

#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISLNK 0120000

typedef struct [[gnu::packed]] {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
} cpio_newc_header_t;

static uint64_t parse_hex(const char* str, int len) {
    uint64_t val = 0;
    for (int i = 0; i < len; i++) {
        val <<= 4;
        if (str[i] >= '0' && str[i] <= '9')
            val += str[i] - '0';
        else if (str[i] >= 'a' && str[i] <= 'f')
            val += str[i] - 'a' + 10;
        else if (str[i] >= 'A' && str[i] <= 'F')
            val += str[i] - 'A' + 10;
    }
    return val;
}

void initrd_unpack(void* addr, size_t size) {
    uintptr_t current = (uintptr_t) addr;
    uintptr_t end = current + size;

    logln(LOG_INFO, "INITRD", "Unpacking initrd from address %#p, size %lu", addr, size);

    while (current < end) {
        cpio_newc_header_t* header = (cpio_newc_header_t*) current;

        // 1. Verify Magic Number ("070701")
        if (!CPIO_MAGIC_OK(header->c_magic)) {
            logln(LOG_WARN, "INITRD", "Invalid magic at %#p", current);
            break;
        }

        // 2. Parse sizes and metadata
        uint64_t mode = parse_hex(header->c_mode, 8);
        uint64_t filesize = parse_hex(header->c_filesize, 8);
        uint64_t namesize = parse_hex(header->c_namesize, 8);

        // 3. Extract the filename
        current += sizeof(cpio_newc_header_t);
        const char* filename = (const char*) current;

        // 4. Check for End of Archive
        if (strcmp(filename, "TRAILER!!!") == 0) {
            logln(LOG_INFO, "INITRD", "End of archive reached.");
            break;
        }

        // 5. Calculate data pointers using your ALIGN_UP macro (4-byte boundaries)
        current = ALIGN_UP(current + namesize, 4);
        void* file_data = (void*) current;

        // Advance 'current' to the start of the next header
        current = ALIGN_UP(current + filesize, 4);

        // --- VFS POPULATION ---

        // Skip the root entry "."
        if (strcmp(filename, ".") == 0)
            continue;

        // Create a path relative to the VFS root. No string copying!
        // E.g., CPIO path "bin/init" becomes relative to vfs_get_root()
        path_t target_path = REL_PATH(vfs_get_root(), filename);

        int file_type = mode & S_IFMT;

        switch (file_type) {
            case S_IFDIR: {
                ASSERT(vfs_mkdir(target_path) == 0);
                break;
            }

            case S_IFREG: {
                // 1. Create the File (Allocate vnode and tmpfs_node)
                ASSERT(vfs_create(target_path) == 0);

                // 2. Lookup the newly created node
                vnode_t* node = nullptr;
                ASSERT(vfs_lookup(target_path, &node) == 0);
                if (node->type == V_REG) {
                    // 3. ZERO-COPY HACK: Point the tmpfs_node directly to the initrd memory
                    tmpfs_node_t* t_node = (tmpfs_node_t*) node->private;
                    t_node->file.base = file_data;
                    t_node->file.size = filesize;

                    // logln(LOG_DEBUG, "INITRD", "Created File: /%s (%lu bytes)", filename, filesize);
                }
                break;
            }

            case S_ISLNK: {
                // logln(LOG_DEBUG, "INITRD", "LINK: /%s (%lu bytes)", filename, filesize);
                break;
            }

            default: {
                logln(LOG_DEBUG, "INITRD", "FILE_TYPE ???");
            }
        }
    }

    logln(LOG_INFO, "INITRD", "Successfully unpacked to tmpfs.");
}
