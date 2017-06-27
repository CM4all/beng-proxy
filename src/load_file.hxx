/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LOAD_FILE_HXX
#define LOAD_FILE_HXX

#include <sys/types.h>

template<typename T> struct ConstBuffer;
struct pool;

/**
 * Load the contents of a file into a buffer.
 *
 * Throws exception on error.
 */
ConstBuffer<void>
LoadFile(struct pool &pool, const char *path, off_t max_size);

#endif
