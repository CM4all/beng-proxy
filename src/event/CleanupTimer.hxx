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
    void Init(EventLoop &loop, unsigned delay_s,
              bool (*callback)(void *ctx), void *ctx);
    void Deinit() {
        event.Deinit();
    }

    /**
     * Check if the event was initialized.  Calling this method is
     * only legal if it really was initialized or if the memory is
     * zeroed (e.g. an uninitialized global/static variable).
     */
    gcc_pure
    bool IsInitialized() const {
        return event.IsInitialized();
    }

    void Enable();
    void Disable();

private:
    void OnTimer();
};

#endif
