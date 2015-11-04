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
class Istream;

Istream *
istream_catch_new(struct pool *pool, Istream &input,
                  GError *(*callback)(GError *error, void *ctx), void *ctx);

#endif
