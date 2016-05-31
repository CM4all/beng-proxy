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

    BoundMethod<void()> callback;

public:
    TimerEvent() = default;

    TimerEvent(EventLoop &loop, event_callback_fn _callback, void *ctx)
        :event(loop, -1, 0, _callback, ctx) {}

    TimerEvent(EventLoop &loop, BoundMethod<void()> _callback)
        :event(loop, -1, 0, Callback, this), callback(_callback) {}

    TimerEvent(event_callback_fn _callback, void *ctx) {
        event.SetTimer(_callback, ctx);
    }

    void Init(EventLoop &loop, event_callback_fn _callback, void *ctx) {
        event.Set(loop, -1, 0, _callback, ctx);
    }

    void Init(event_callback_fn _callback, void *ctx) {
        event.SetTimer(_callback, ctx);
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

private:
    static void Callback(gcc_unused evutil_socket_t fd,
                         gcc_unused short events,
                         void *ctx) {
        auto &event = *(TimerEvent *)ctx;
        event.callback();
    }
};

#endif
