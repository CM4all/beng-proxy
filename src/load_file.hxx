/*
 * Load the contents of a file into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LOAD_FILE_HXX
#define LOAD_FILE_HXX

#include "glibfwd.hxx"

#include <sys/types.h>

template<typename T> struct ConstBuffer;
struct pool;

ConstBuffer<void>
LoadFile(struct pool &pool, const char *path, off_t max_size, GError **error_r);

#endif
