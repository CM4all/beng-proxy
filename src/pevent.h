/*
 * Wrapper for event.h which registers the "event" object as a pool
 * attachment.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PEVENT_H
#define BENG_PROXY_PEVENT_H

#include "pool.h"

#include <assert.h>
#include <event.h>

static inline void
p_event_add(struct event *ev, const struct timeval *timeout,
            pool_t pool, const char *name)
{
    assert(ev != NULL);
    assert(pool != NULL);
    assert(pool_contains(pool, ev, sizeof(*ev)));
    assert(name != NULL);

    pool_attach_checked(pool, ev, name);

    event_add(ev, timeout);
}

static inline void
p_event_del(struct event *ev, pool_t pool)
{
    assert(ev != NULL);
    assert(pool != NULL);
    assert(pool_contains(pool, ev, sizeof(*ev)));

    pool_detach_checked(pool, ev);
    event_del(ev);
}

static inline void
p_event_consumed(struct event *ev, pool_t pool)
{
    assert(ev != NULL);
    assert(pool != NULL);
    assert(pool_contains(pool, ev, sizeof(*ev)));

    pool_detach(pool, ev);
}

#endif
