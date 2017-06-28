/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_INJECT_HXX
#define BENG_PROXY_ISTREAM_INJECT_HXX

#include <exception>

struct pool;
class Istream;

/**
 * istream implementation which produces a failure.
 */
Istream *
istream_inject_new(struct pool &pool, Istream &input);

void
istream_inject_fault(Istream &i_fault, std::exception_ptr ep);

#endif
