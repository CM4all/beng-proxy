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
