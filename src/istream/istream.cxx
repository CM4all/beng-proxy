/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.hxx"

Istream::~Istream()
{
    assert(!destroyed);
    destroyed = true;

    pool_unref(&pool);
}
