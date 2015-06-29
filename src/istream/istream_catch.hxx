/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CATCH_HXX
#define BENG_PROXY_ISTREAM_CATCH_HXX

#include "glibfwd.hxx"

struct pool;
struct istream;

struct istream *
istream_catch_new(struct pool *pool, struct istream *input,
                  GError *(*callback)(GError *error, void *ctx), void *ctx);

#endif
