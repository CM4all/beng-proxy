/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DEFER_EVENT_HXX
#define BENG_PROXY_DEFER_EVENT_HXX

#include "Event.hxx"

/**
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 */
class DeferEvent {
    Event event;

public:
    DeferEvent() = default;
    DeferEvent(event_callback_fn callback, void *ctx) {
        event.SetTimer(callback, ctx);
    }

    void Init(event_callback_fn callback, void *ctx) {
        event.SetTimer(callback, ctx);
    }

    void Deinit() {
        Cancel();
    }

    void Add() {
        static constexpr struct timeval tv = { 0, 0};
        event.Add(tv);
    }

    void Cancel() {
        event.Delete();
    }
};

#endif
