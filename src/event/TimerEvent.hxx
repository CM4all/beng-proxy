/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TIMER_EVENT_HXX
#define BENG_PROXY_TIMER_EVENT_HXX

#include "Event.hxx"
#include "util/BindMethod.hxx"

/**
 * Invoke an event callback after a certain amount of time.
 */
class TimerEvent {
    Event event;

    const BoundMethod<void()> callback;

public:
    TimerEvent(EventLoop &loop, BoundMethod<void()> _callback)
        :event(loop, -1, 0, Callback, this), callback(_callback) {}

    bool IsPending() const {
        return event.IsTimerPending();
    }

    void Add(const struct timeval &tv) {
        event.Add(tv);
    }

    void Cancel() {
        event.Delete();
    }

private:
    static void Callback(gcc_unused evutil_socket_t fd,
                         gcc_unused short events,
                         void *ctx) {
        auto &event = *(TimerEvent *)ctx;
        event.callback();
    }
};

#endif
