/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LightDeferEvent.hxx"
#include "Loop.hxx"

void
LightDeferEvent::Schedule()
{
    if (!IsPending())
        loop.Defer(*this);

    assert(IsPending());
}

void
LightDeferEvent::Cancel()
{
    if (IsPending())
        loop.CancelDefer(*this);

    assert(!IsPending());
}
