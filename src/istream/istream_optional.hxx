/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_OPTIONAL_HXX
#define BENG_PROXY_ISTREAM_OPTIONAL_HXX

struct pool;
class Istream;

/**
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 */
Istream *
istream_optional_new(struct pool &pool, Istream &input);

/**
 * Allows the istream to resume, but does not trigger reading.
 */
void
istream_optional_resume(Istream &istream);

/**
 * Discard the stream contents.
 */
void
istream_optional_discard(Istream &istream);

#endif
