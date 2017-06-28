/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DELAYED_HXX
#define BENG_PROXY_ISTREAM_DELAYED_HXX

#include <exception>

struct pool;
class Istream;
class CancellablePointer;

/**
 * An istream facade which waits for its inner istream to appear.
 */
Istream *
istream_delayed_new(struct pool *pool);

CancellablePointer &
istream_delayed_cancellable_ptr(Istream &i_delayed);

void
istream_delayed_set(Istream &istream_delayed, Istream &input);

void
istream_delayed_set_eof(Istream &istream_delayed);

/**
 * Injects a failure, to be called instead of istream_delayed_set().
 */
void
istream_delayed_set_abort(Istream &istream_delayed, std::exception_ptr ep);

#endif
