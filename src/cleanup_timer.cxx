/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cleanup_timer.hxx"

#include <inline/compiler.h>

#include <stddef.h>

static void
cleanup_timer_event_callback(gcc_unused int fd, gcc_unused short event,
                             void *ctx)
{
    CleanupTimer *t = (CleanupTimer *)ctx;

    if (t->callback(t->callback_ctx))
        cleanup_timer_enable(t);
}

void
cleanup_timer_init(CleanupTimer *t, unsigned delay_s,
                   bool (*callback)(void *ctx), void *ctx)
{
    evtimer_set(&t->event, cleanup_timer_event_callback, t);

    t->delay.tv_sec = delay_s;
    t->delay.tv_usec = 0;

    t->callback = callback;
    t->callback_ctx = ctx;
}

void
cleanup_timer_enable(CleanupTimer *t)
{
    if (!evtimer_pending(&t->event, NULL))
        event_add(&t->event, &t->delay);
}

void
cleanup_timer_disable(CleanupTimer *t)
{
    event_del(&t->event);
}
