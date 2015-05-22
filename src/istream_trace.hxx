/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_TRACE_HXX
#define BENG_PROXY_ISTREAM_TRACE_HXX

struct pool;
struct istream;

/**
 * This istream filter prints debug information to stderr.
 */
struct istream *
istream_trace_new(struct pool *pool, struct istream *input);

#endif
