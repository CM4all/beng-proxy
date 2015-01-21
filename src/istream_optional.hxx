/*
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_OPTIONAL_HXX
#define BENG_PROXY_ISTREAM_OPTIONAL_HXX

struct pool;
struct istream;

struct istream *
istream_optional_new(struct pool *pool, struct istream *input);

#endif
