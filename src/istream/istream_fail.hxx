/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FAIL_HXX
#define BENG_PROXY_ISTREAM_FAIL_HXX

#include "glibfwd.hxx"

struct pool;
struct istream;

/**
 * istream implementation which produces a failure.
 */
struct istream *
istream_fail_new(struct pool *pool, GError *error);

#endif
