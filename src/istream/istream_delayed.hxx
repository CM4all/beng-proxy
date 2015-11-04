/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DELAYED_HXX
#define BENG_PROXY_ISTREAM_DELAYED_HXX

#include "glibfwd.hxx"

struct pool;
class Istream;

/**
 * An istream facade which waits for its inner istream to appear.
 */
Istream *
istream_delayed_new(struct pool *pool);

struct async_operation_ref *
istream_delayed_async_ref(Istream &i_delayed);

void
istream_delayed_set(Istream &istream_delayed, Istream &input);

void
istream_delayed_set_eof(Istream &istream_delayed);

/**
 * Injects a failure, to be called instead of istream_delayed_set().
 */
void
istream_delayed_set_abort(Istream &istream_delayed, GError *error);

#endif
