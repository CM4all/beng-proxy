/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CleanupTimer.hxx"

void
CleanupTimer::OnTimer()
{
    if (callback())
        Enable();
}

void
CleanupTimer::Enable()
{
    if (!event.IsPending())
        event.Add(delay);
}

void
CleanupTimer::Disable()
{
    event.Cancel();
}
