/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_EVENT2_H
#define __BENG_EVENT2_H

#include <inline/compiler.h>

#include <sys/types.h>
#include <assert.h>
#include <event.h>

struct event2 {
    struct event event;
    unsigned locked;
    short always_mask, new_mask, old_mask;
    int fd;
    void (*callback)(int, short, void *arg);
    void *ctx;
    const struct timeval *tv;
};

void
event2_init(struct event2 *event, int fd,
            void (*callback)(int, short, void *arg), void *ctx,
            const struct timeval *tv);

void
event2_commit(struct event2 *event);

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
    assert((event->always_mask & EV_PERSIST) == 0);

    event->old_mask = 0;
    event->new_mask = 0;
}

static inline void
event2_persist(struct event2 *event)
{
    assert((event->always_mask & EV_PERSIST) == 0);

    event->always_mask |= EV_PERSIST;
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
