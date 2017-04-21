/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "PInstance.hxx"
#include "pool.hxx"

PInstance::PInstance()
{
#ifndef NDEBUG
    event_loop.SetPostCallback(BIND_FUNCTION(pool_commit));
#endif
}

PInstance::~PInstance()
{
}
