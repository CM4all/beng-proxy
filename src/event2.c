/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "event2.h"

#include <stddef.h>

void
event2_init(struct event2 *event, int fd,
            void (*callback)(int, short, void *arg), void *ctx,
            const struct timeval *tv)
{
    assert(event != NULL);
    assert(fd >= 0);
    assert(callback != NULL);

    event->locked = 0;
    event->always_mask = 0;
    event->new_mask = 0;
    event->old_mask = 0;
    event->fd = fd;
    event->callback = callback;
    event->ctx = ctx;
    event->tv = tv;

    if (tv != NULL)
        event->always_mask |= EV_TIMEOUT;
}

void
event2_commit(struct event2 *event)
{
    if (event->new_mask != event->old_mask) {
        if (event->old_mask != 0)
            event_del(&event->event);

        if (event->new_mask != 0) {
            short mask = event->new_mask | event->always_mask;

            event_set(&event->event, event->fd, mask,
                      event->callback, event->ctx);
            event_add(&event->event, event->tv);
        }

        event->old_mask = event->new_mask;
    }
}
