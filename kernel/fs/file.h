#pragma once

#include "common/lock/mutex.h"
#include "fs/vfs.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define AT_FDCWD -100
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_ASYNC 020000
#define O_DIRECT 040000
#define O_LARGEFILE 0100000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_NOATIME 01000000
#define O_CLOEXEC 02000000
#define O_SYNC 04010000
#define O_RSYNC 04010000
#define O_TMPFILE 020000000


typedef unsigned int mode_t;

typedef struct {
    vnode_t* vnode;
    mutex_t mutex;
    int refcount;
    mode_t mode;
    uintmax_t offset;
    int flags;
} file_t;

void file_ref(file_t* file);
void file_put(file_t* file);

file_t* file_alloc(vnode_t* vnode, int flags);

ssize_t file_read(file_t* file, void* buf, size_t count);
ssize_t file_write(file_t* file, const void* buf, size_t count);
off_t file_seek(file_t* file, off_t offset, int whence);

void file_init();
