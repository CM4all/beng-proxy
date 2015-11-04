/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.hxx"
#include "Bucket.hxx"

Istream::~Istream()
{
#ifndef NDEBUG
    assert(!destroyed);
    destroyed = true;
#endif

    pool_unref(&pool);
}

bool
Istream::_FillBucketList(IstreamBucketList &list, GError **)
{
    list.SetMore();
    return true;
}
