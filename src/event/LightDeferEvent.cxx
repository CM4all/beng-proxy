/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LightDeferEvent.hxx"
#include "Loop.hxx"

void
LightDeferEvent::Schedule()
{
    if (!siblings.is_linked())
        loop.Defer(*this);

    assert(siblings.is_linked());
}

void
LightDeferEvent::Cancel()
{
    if (siblings.is_linked())
        loop.CancelDefer(*this);

    assert(!siblings.is_linked());
}
