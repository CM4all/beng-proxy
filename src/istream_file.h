/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FILE_H
#define BENG_PROXY_ISTREAM_FILE_H

#include "istream-direct.h"

#include <inline/compiler.h>

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

struct pool;
struct stat;

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
istream_file_fd_new(struct pool *pool, const char *path,
                    int fd, enum istream_direct fd_type, off_t length);

/**
 * Opens a file and stats it.
 */
struct istream *
istream_file_stat_new(struct pool *pool, const char *path, struct stat *st);

struct istream * gcc_malloc
istream_file_new(struct pool *pool, const char *path, off_t length);

int
istream_file_fd(struct istream * istream);

/**
 * Select a range of the file.  This must be the first call after
 * creating the object.
 */
bool
istream_file_set_range(struct istream *istream, off_t start, off_t end);

#ifdef __cplusplus
}
#endif

#endif
