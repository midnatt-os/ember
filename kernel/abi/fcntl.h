#pragma once

/**
 * @file fcntl.h
 * @brief File control options and flags (POSIX ABI)
 */

/* Open flags (only one of O_RDONLY, O_WRONLY, O_RDWR) */
#define O_RDONLY    0x0000  /**< open for reading only */
#define O_WRONLY    0x0001  /**< open for writing only */
#define O_RDWR      0x0002  /**< open for reading and writing */

/* File creation and status flags */
#define O_CREAT     0x0100  /**< create file if it does not exist */
#define O_EXCL      0x0200  /**< error if O_CREAT and the file exists */
#define O_NOCTTY    0x0400  /**< do not assign controlling terminal */
#define O_TRUNC     0x0800  /**< truncate size to 0 */
#define O_APPEND    0x1000  /**< append on each write */
#define O_NONBLOCK  0x2000  /**< non-blocking mode */

/* fcntl() command values */
#define F_DUPFD     0   /**< duplicate file descriptor */
#define F_GETFD     1   /**< get file descriptor flags */
#define F_SETFD     2   /**< set file descriptor flags */
#define F_GETFL     3   /**< get file status flags */
#define F_SETFL     4   /**< set file status flags */
#define F_GETLK     5   /**< get record locking information */
#define F_SETLK     6   /**< set record locking information (non-blocking) */
#define F_SETLKW    7   /**< set record locking information (blocking) */

/* File descriptor flags for F_GETFD/F_SETFD */
#define FD_CLOEXEC  1   /**< close-on-exec flag */

/* Advisory locking types for struct flock */
#define F_RDLCK     0   /**< shared or read lock */
#define F_WRLCK     1   /**< exclusive or write lock */
#define F_UNLCK     2   /**< unlock */

/**
 * struct flock
 *
 * Used with F_GETLK/F_SETLK/F_SETLKW to describe a file lock.

struct flock {
    short    l_type;    //< F_RDLCK, F_WRLCK, or F_UNLCK
    short    l_whence;  //< how to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END
    off_t    l_start;   //< starting offset for lock
    off_t    l_len;     //< number of bytes to lock; 0 means to EOF
    pid_t    l_pid;     //< returned with F_GETLK: PID of blocking process
};
*/
