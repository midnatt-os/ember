#include "common/tar.h"

#include <stdint.h>
#include <stddef.h>

#include "nanoprintf.h"
#include "common/assert.h"
#include "common/log.h"
#include "common/panic.h"
#include "common/util.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "memory/heap.h"

#define BLOCK_SIZE 512

typedef struct [[gnu::packed]] {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} UstarHeader;



static uint64_t parse_file_size(const char* src, size_t len) {
    uint64_t value = 0;

    for (size_t i = 0; i < len; i++) {
        char c = src[i];

        if (c < '0' || c > '7')
            break;

        value = (value << 3) | (c - '0');
    }

    return value;
}

void populate_tmpfs_from_initrd(Module* initrd_module) {
    uintptr_t cursor = (uintptr_t) initrd_module->address;
    uintptr_t archive_end = cursor + initrd_module->size;

    while (cursor + sizeof(UstarHeader) <= archive_end) {
        UstarHeader* header = (UstarHeader*) cursor;

        // End of archive
        if (header->name[0] == '\0')
            break;

        char* filename = kmalloc(256);
        if (header->prefix[0] != '\0') {
            npf_snprintf(filename, 256, "/%s/%s", header->prefix, header->name);
        } else {
            npf_snprintf(filename, 256, "/%s", header->name);
        }

        uint64_t file_size = parse_file_size(header->size, sizeof(header->size));
        char type = header->typeflag;

        // \0: file ; 5: dir
        ASSERT(type != '\0' || type != '5');

        if (type == '5') {
            VNode* new_node;
            vfs_create_dir(filename, &new_node);
        } else {
            void* data = (void*) cursor + BLOCK_SIZE;

            VNode* new_node;
            size_t written;
            vfs_create_file(filename, &new_node);
            vfs_write(filename, data, 0, file_size, &written);
        }

        cursor += BLOCK_SIZE + ALIGN_UP(file_size, BLOCK_SIZE);
    }

    logln(LOG_INFO, "INITRD", "Unpacked initrd module into root tmpfs");
}
