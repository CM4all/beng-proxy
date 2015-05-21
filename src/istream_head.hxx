/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_HEAD_HXX
#define BENG_PROXY_ISTREAM_HEAD_HXX

#include <stddef.h>

struct pool;
struct istream;

/**
 * This istream filter passes only the first N bytes.
 *
 * @param authoritative is the specified size authoritative?
 */
struct istream *
istream_head_new(struct pool *pool, struct istream *input,
                 size_t size, bool authoritative);

#endif
