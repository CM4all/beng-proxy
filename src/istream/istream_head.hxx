/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_HEAD_HXX
#define BENG_PROXY_ISTREAM_HEAD_HXX

#include <stddef.h>

struct pool;
class Istream;

/**
 * This istream filter passes only the first N bytes.
 *
 * @param authoritative is the specified size authoritative?
 */
Istream *
istream_head_new(struct pool *pool, Istream &input,
                 size_t size, bool authoritative);

#endif
