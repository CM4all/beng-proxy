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
}

void
Istream::_FillBucketList(IstreamBucketList &list)
{
    list.SetMore();
}

gcc_noreturn
size_t
Istream::_ConsumeBucketList(gcc_unused size_t nbytes)
{
    assert(false);
    gcc_unreachable();
}
