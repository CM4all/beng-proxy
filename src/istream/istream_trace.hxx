/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_TRACE_HXX
#define BENG_PROXY_ISTREAM_TRACE_HXX

struct pool;
class Istream;

/**
 * This istream filter prints debug information to stderr.
 */
Istream *
istream_trace_new(struct pool *pool, Istream &input);

#endif
