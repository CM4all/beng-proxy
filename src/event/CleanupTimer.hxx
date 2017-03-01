/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLEANUP_TIMER_HXX
#define BENG_PROXY_CLEANUP_TIMER_HXX

#include "TimerEvent.hxx"

/**
 * Wrapper for #TimerEvent which aims to simplify installing recurring
 * events.
 */
class CleanupTimer {
    TimerEvent event;

    const struct timeval delay;

    /**
     * @return true if another cleanup shall be scheduled
     */
    typedef BoundMethod<bool()> Callback;
    const Callback callback;

public:
    CleanupTimer(EventLoop &loop, unsigned delay_s,
                 Callback _callback)
        :event(loop, BIND_THIS_METHOD(OnTimer)),
         delay{time_t(delay_s), 0},
         callback(_callback) {}

    ~CleanupTimer() {
        event.Cancel();
    }

    void Enable();
    void Disable();

private:
    void OnTimer();
};

#endif
