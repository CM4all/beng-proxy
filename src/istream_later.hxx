/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_LATER_HXX
#define BENG_PROXY_ISTREAM_LATER_HXX

struct pool;
struct istream;

/**
 * An istream filter which delays the read() and eof() invocations.
 * This is used in the test suite.
 */
struct istream *
istream_later_new(struct pool *pool, struct istream *input);

#endif
