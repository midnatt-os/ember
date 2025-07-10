#pragma once

#include "common/lock/mutex.h"
#include "file.h"

#define MAX_FDS 256

typedef struct {
    File* fds[MAX_FDS];
    Mutex lock;
} FDTable;

void fd_init(FDTable* table);
void fd_clone_table(FDTable* to_clone, FDTable* copy);
int fd_alloc(FDTable* table, File* file);
int fd_get(FDTable* table, int fd, File** file);
int fd_close(FDTable* table, int fd);
int fd_dup(FDTable* table, int fd);
int fd_dup2(FDTable* table, int fd, int newfd);
