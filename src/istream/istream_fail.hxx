/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FAIL_HXX
#define BENG_PROXY_ISTREAM_FAIL_HXX

#include <exception>

struct pool;
class Istream;

/**
 * istream implementation which produces a failure.
 */
Istream *
istream_fail_new(struct pool *pool, std::exception_ptr ep);

#endif
