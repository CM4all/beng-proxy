/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_STRING_HXX
#define BENG_PROXY_ISTREAM_STRING_HXX

struct pool;
struct istream;

/**
 * istream implementation which reads from a string.
 */
struct istream *
istream_string_new(struct pool *pool, const char *s);

#endif
