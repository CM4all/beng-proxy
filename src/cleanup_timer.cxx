/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cleanup_timer.hxx"

#include <inline/compiler.h>

#include <stddef.h>

void
CleanupTimer::Callback(gcc_unused int fd, gcc_unused short event, void *ctx)
{
    CleanupTimer *t = (CleanupTimer *)ctx;

    if (t->callback(t->callback_ctx))
        t->Enable();
}

void
CleanupTimer::Init(unsigned delay_s, bool (*_callback)(void *ctx), void *_ctx)
{
    evtimer_set(&event, Callback, this);

    delay.tv_sec = delay_s;
    delay.tv_usec = 0;

    callback = _callback;
    callback_ctx = _ctx;
}

void
CleanupTimer::Enable()
{
    if (!evtimer_pending(&event, nullptr))
        event_add(&event, &delay);
}

void
CleanupTimer::Disable()
{
    event_del(&event);
}
