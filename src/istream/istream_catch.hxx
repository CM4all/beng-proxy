/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CATCH_HXX
#define BENG_PROXY_ISTREAM_CATCH_HXX

#include <exception>

struct pool;
class Istream;

Istream *
istream_catch_new(struct pool *pool, Istream &input,
                  std::exception_ptr (*callback)(std::exception_ptr ep, void *ctx), void *ctx);

#endif
