/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TIMER_EVENT_HXX
#define BENG_PROXY_TIMER_EVENT_HXX

#include "Event.hxx"

/**
 * Invoke an event callback after a certain amount of time.
 */
class TimerEvent {
    Event event;

public:
    TimerEvent() = default;
    TimerEvent(event_callback_fn callback, void *ctx) {
        event.SetTimer(callback, ctx);
    }

    void Init(event_callback_fn callback, void *ctx) {
        event.SetTimer(callback, ctx);
    }

    void Deinit() {
        Cancel();
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

    bool IsPending() const {
        return event.IsTimerPending();
    }

    void Add(const struct timeval &tv) {
        event.Add(tv);
    }

    void Cancel() {
        event.Delete();
    }
};

#endif
