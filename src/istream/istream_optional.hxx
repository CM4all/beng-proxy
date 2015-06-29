/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_OPTIONAL_HXX
#define BENG_PROXY_ISTREAM_OPTIONAL_HXX

struct pool;
struct istream;

/**
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 */
struct istream *
istream_optional_new(struct pool *pool, struct istream *input);

/**
 * Allows the istream to resume, but does not trigger reading.
 */
void
istream_optional_resume(struct istream *istream);

/**
 * Discard the stream contents.
 */
void
istream_optional_discard(struct istream *istream);

#endif
