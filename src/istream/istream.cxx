/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.hxx"

istream::istream(struct pool &_pool)
    :pool(_pool)
{
    pool_ref(&pool);
}

istream::~istream()
{
    assert(!destroyed);
    destroyed = true;

    pool_unref(&pool);
}

Istream::~Istream()
{
}
