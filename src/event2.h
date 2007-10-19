/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_EVENT2_H
#define __BENG_EVENT2_H

#include "compiler.h"

#include <assert.h>
#include <event.h>

struct event2 {
    struct event event;
    unsigned locked;
    short new_mask, old_mask;
    int fd;
    void (*callback)(int, short, void *arg);
    void *ctx;
    const struct timeval *tv;
};

static inline void
event2_init(struct event2 *event, int fd,
            void (*callback)(int, short, void *arg), void *ctx,
            const struct timeval *tv)
{
    assert(event != NULL);
    assert(fd >= 0);
    assert(callback != NULL);

    event->locked = 0;
    event->new_mask = 0;
    event->old_mask = 0;
    event->fd = fd;
    event->callback = callback;
    event->ctx = ctx;
    event->tv = tv;
}

static inline void
event2_commit(struct event2 *event)
{
    if (event->new_mask != event->old_mask) {
        if (event->old_mask != 0)
            event_del(&event->event);

        if (event->new_mask != 0) {
            short mask = event->new_mask;
            struct timeval tv;

            if (event->tv != NULL) {
                tv = *event->tv;
                mask |= EV_TIMEOUT;
            }

            event_set(&event->event, event->fd, mask,
                      event->callback, event->ctx);
            event_add(&event->event,
                      event->tv != NULL ? &tv : NULL);
        }

        event->old_mask = event->new_mask;
    }
}

static inline void
event2_lock(struct event2 *event)
{
    ++event->locked;
}

static inline void
event2_unlock(struct event2 *event)
{
    assert(event->locked > 0);

    --event->locked;
    if (event->locked == 0)
        event2_commit(event);
}

static inline void
event2_reset(struct event2 *event)
{
    event->old_mask = 0;
    event->new_mask = 0;
}

static inline void
event2_set(struct event2 *event, short mask)
{
    event->new_mask = mask;
    if (event->locked == 0)
        event2_commit(event);
}

static inline void
event2_or(struct event2 *event, short mask)
{
    /* icc complains when we use "|=" */
    event->new_mask = (short)(event->new_mask | mask);
    if (event->locked == 0)
        event2_commit(event);
}

static inline void
event2_nand(struct event2 *event, short mask)
{
    /* icc complains when we use "&=~" */
    event->new_mask = (short)(event->new_mask & ~mask);
    if (event->locked == 0)
        event2_commit(event);
}

static inline void
event2_setbit(struct event2 *event, short mask, int condition)
{
    if (condition)
        event2_or(event, mask);
    else
        event2_nand(event, mask);
}

#endif
