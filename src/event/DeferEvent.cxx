/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "DeferEvent.hxx"
#include "Loop.hxx"

void
DeferEvent::Schedule()
{
    if (!IsPending())
        loop.Defer(*this);

    assert(IsPending());
}

void
DeferEvent::Cancel()
{
    if (IsPending())
        loop.CancelDefer(*this);

    assert(!IsPending());
}
