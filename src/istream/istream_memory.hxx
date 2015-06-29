/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_MEMORY_HXX
#define BENG_PROXY_ISTREAM_MEMORY_HXX

#include <stddef.h>

struct pool;
struct istream;

/**
 * istream implementation which reads from a fixed memory buffer.
 */
struct istream *
istream_memory_new(struct pool *pool, const void *data, size_t length);

#endif
