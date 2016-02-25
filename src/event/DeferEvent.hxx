/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DEFER_EVENT_HXX
#define BENG_PROXY_DEFER_EVENT_HXX

#include "TimerEvent.hxx"
#include "Duration.hxx"

/**
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 */
class DeferEvent : TimerEvent {
public:
    DeferEvent() = default;
    DeferEvent(event_callback_fn callback, void *ctx)
        :TimerEvent(callback, ctx) {}

    using TimerEvent::Init;
    using TimerEvent::Deinit;
    using TimerEvent::IsPending;

    void Add() {
        TimerEvent::Add(EventDuration<0>::value);
    }

    using TimerEvent::Cancel;
};

#endif
