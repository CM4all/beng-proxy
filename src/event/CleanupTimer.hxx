/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLEANUP_TIMER_HXX
#define BENG_PROXY_CLEANUP_TIMER_HXX

#include "TimerEvent.hxx"

class CleanupTimer {
    TimerEvent event;

    struct timeval delay;

    /**
     * @return true if another cleanup shall be scheduled
     */
    bool (*callback)(void *ctx);
    void *callback_ctx;

public:
    void Init(unsigned delay_s,
              bool (*callback)(void *ctx), void *ctx);
    void Deinit() {
        event.Deinit();
    }

    void Enable();
    void Disable();

private:
    void OnTimer();
};

#endif
