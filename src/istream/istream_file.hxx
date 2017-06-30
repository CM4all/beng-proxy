/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FILE_HXX
#define BENG_PROXY_ISTREAM_FILE_HXX

#include "io/FdType.hxx"

#include "util/Compiler.h"

#include <sys/types.h>

struct pool;
struct stat;
class Istream;
class EventLoop;

Istream *
istream_file_fd_new(EventLoop &event_loop, struct pool &pool,
                    const char *path,
                    int fd, FdType fd_type, off_t length);

/**
 * Opens a file and stats it.
 *
 * Throws exception on error.
 */
Istream *
istream_file_stat_new(EventLoop &event_loop, struct pool &pool,
                      const char *path, struct stat &st);

/**
 * Throws exception on error.
 */
Istream *
istream_file_new(EventLoop &event_loop, struct pool &pool,
                 const char *path, off_t length);

int
istream_file_fd(Istream &istream);

/**
 * Select a range of the file.  This must be the first call after
 * creating the object.
 */
bool
istream_file_set_range(Istream &istream, off_t start, off_t end);

#endif
