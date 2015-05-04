/*
 * An istream filter that escapes the data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_ESCAPE_HXX
#define BENG_PROXY_ISTREAM_ESCAPE_HXX

struct pool;
struct istream;
struct escape_class;

struct istream *
istream_escape_new(struct pool *pool, struct istream *input,
                   const struct escape_class *cls);

#endif
