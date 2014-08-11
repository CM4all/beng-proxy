/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLEANUP_TIMER_HXX
#define BENG_PROXY_CLEANUP_TIMER_HXX

#include <event.h>

class CleanupTimer {
public:
    struct event event;

    struct timeval delay;

    /**
     * @return true if another cleanup shall be scheduled
     */
    bool (*callback)(void *ctx);
    void *callback_ctx;
};

void
cleanup_timer_init(CleanupTimer *t, unsigned delay_s,
                   bool (*callback)(void *ctx), void *ctx);

void
cleanup_timer_enable(CleanupTimer *t);

void
cleanup_timer_disable(CleanupTimer *t);

static inline void
cleanup_timer_deinit(CleanupTimer *t)
{
    cleanup_timer_disable(t);
}

#endif
