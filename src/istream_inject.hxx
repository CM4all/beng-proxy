/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_INJECT_HXX
#define BENG_PROXY_ISTREAM_INJECT_HXX

#include "glibfwd.hxx"

struct pool;
struct istream;

/**
 * istream implementation which produces a failure.
 */
struct istream *
istream_inject_new(struct pool *pool, struct istream *input);

void
istream_inject_fault(struct istream *i_fault, GError *error);

#endif
