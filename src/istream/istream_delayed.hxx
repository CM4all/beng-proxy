/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DELAYED_HXX
#define BENG_PROXY_ISTREAM_DELAYED_HXX

#include "glibfwd.hxx"

struct pool;
struct istream;

/**
 * An istream facade which waits for its inner istream to appear.
 */
struct istream *
istream_delayed_new(struct pool *pool);

struct async_operation_ref *
istream_delayed_async_ref(struct istream *i_delayed);

void
istream_delayed_set(struct istream *istream_delayed, struct istream *input);

void
istream_delayed_set_eof(struct istream *istream_delayed);

/**
 * Injects a failure, to be called instead of istream_delayed_set().
 */
void
istream_delayed_set_abort(struct istream *istream_delayed, GError *error);

#endif
