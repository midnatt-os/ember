#pragma once

#include "common/lock/mutex.h"
#include "fs/file.h"

#define MAX_FDS 256

typedef struct {
    file_t* file;
    int flags; // e.g. O_CLOEXEC
} fd_entry_t;

typedef struct {
    fd_entry_t entries[MAX_FDS];
    mutex_t lock;
    int refcount;
} fd_table_t;

fd_table_t* fd_table_alloc(void);
void fd_table_ref(fd_table_t* table);
void fd_table_put(fd_table_t* table);

int fd_install(fd_table_t* table, file_t* file, int flags);
file_t* fd_get(fd_table_t* table, int fd);
int fd_close(fd_table_t* table, int fd);
int fd_dup(fd_table_t* table, int fd);

void fd_init();
