/*
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DEFER_EVENT_H
#define BENG_PROXY_DEFER_EVENT_H

#include <event.h>

struct defer_event {
    struct event event;
};

static inline void
defer_event_init(struct defer_event *event,
                 void (*callback)(evutil_socket_t, short, void *), void *ctx)
{
    evtimer_set(&event->event, callback, ctx);
}

static inline void
defer_event_add(struct defer_event *event)
{
    const struct timeval tv = { 0, 0};
    evtimer_add(&event->event, &tv);
}

static inline void
defer_event_cancel(struct defer_event *event)
{
    evtimer_del(&event->event);
}

static inline void
defer_event_deinit(struct defer_event *event)
{
    defer_event_cancel(event);
}

#endif
