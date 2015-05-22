/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FOUR_HXX
#define BENG_PROXY_ISTREAM_FOUR_HXX

struct pool;
struct istream;

/**
 * This istream filter passes no more than four bytes at a time.  This
 * is useful for testing and debugging istream handler
 * implementations.
 */
struct istream *
istream_four_new(struct pool *pool, struct istream *input);

#endif
