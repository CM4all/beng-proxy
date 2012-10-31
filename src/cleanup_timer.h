/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLEANUP_TIMER_H
#define BENG_PROXY_CLEANUP_TIMER_H

#include <stdbool.h>
#include <event.h>

struct cleanup_timer {
    struct event event;

    struct timeval delay;

    /**
     * @return true if another cleanup shall be scheduled
     */
    bool (*callback)(void *ctx);
    void *callback_ctx;
};

void
cleanup_timer_init(struct cleanup_timer *t, unsigned delay_s,
                   bool (*callback)(void *ctx), void *ctx);

void
cleanup_timer_enable(struct cleanup_timer *t);

void
cleanup_timer_disable(struct cleanup_timer *t);

static inline void
cleanup_timer_deinit(struct cleanup_timer *t)
{
    cleanup_timer_disable(t);
}

#endif
