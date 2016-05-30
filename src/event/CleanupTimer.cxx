/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CleanupTimer.hxx"
#include "Callback.hxx"

#include <inline/compiler.h>

#include <stddef.h>

inline void
CleanupTimer::OnTimer()
{
    if (callback(callback_ctx))
        Enable();
}

void
CleanupTimer::Init(EventLoop &loop, unsigned delay_s,
                   bool (*_callback)(void *ctx), void *_ctx)
{
    event.Init(loop, MakeSimpleEventCallback(CleanupTimer, OnTimer), this);

    delay.tv_sec = delay_s;
    delay.tv_usec = 0;

    callback = _callback;
    callback_ctx = _ctx;
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
