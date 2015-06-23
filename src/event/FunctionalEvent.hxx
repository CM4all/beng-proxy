/*
 * C++ wrappers for libevent using std::function.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef FUNCTIONAL_EVENT_HXX
#define FUNCTIONAL_EVENT_HXX

#include "Event.hxx"

#include <functional>

#include <event.h>

class FunctionalEvent : Event {
    const std::function<void(int fd, short event)> handler;

public:
    FunctionalEvent(std::function<void(int fd, short event)> _handler)
        :handler(_handler) {
        Set(-1, 0);
    }

    ~FunctionalEvent() {
        Delete();
    }

    void Set(int fd, short mask) {
        Event::Set(fd, mask, Callback, this);
    }

    using Event::Add;

    void SetAdd(int fd, short mask, const struct timeval *timeout=nullptr) {
        Set(fd, mask);
        Add(timeout);
    }

    void SetTimer() {
        Event::SetTimer(Callback, this);
    }

    void SetAddTimer(const struct timeval &timeout) {
        SetTimer();
        Add(&timeout);
    }

    void SetSignal(int sig) {
        Event::SetSignal(sig, Callback, this);
    }

    void SetAddSignal(int sig) {
        SetSignal(sig);
        Add();
    }

    using Event::Delete;
    using Event::IsPending;
    using Event::IsTimerPending;

private:
    static void Callback(int fd, short event, void *ctx);
};

class SignalEvent {
    struct event event;

    const std::function<void()> handler;

public:
    SignalEvent(int sig, std::function<void()> _handler)
        :handler(_handler) {
        ::evsignal_set(&event, sig, Callback, this);
        ::evsignal_add(&event, nullptr);
    }

    ~SignalEvent() {
        Delete();
    }

    void Delete() {
        ::evsignal_del(&event);
    }

private:
    static void Callback(int fd, short event, void *ctx);
};

#endif
