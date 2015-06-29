/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FILE_HXX
#define BENG_PROXY_ISTREAM_FILE_HXX

#include "FdType.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <sys/types.h>

struct pool;
struct stat;

struct istream *
istream_file_fd_new(struct pool *pool, const char *path,
                    int fd, FdType fd_type, off_t length);

/**
 * Opens a file and stats it.
 */
struct istream *
istream_file_stat_new(struct pool *pool, const char *path, struct stat *st,
                      GError **error_r);

struct istream * gcc_malloc
istream_file_new(struct pool *pool, const char *path, off_t length,
                 GError **error_r);

int
istream_file_fd(struct istream * istream);

/**
 * Select a range of the file.  This must be the first call after
 * creating the object.
 */
bool
istream_file_set_range(struct istream *istream, off_t start, off_t end);

#endif
