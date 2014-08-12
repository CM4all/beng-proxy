/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SNOWBALL_EVENT_HXX
#define SNOWBALL_EVENT_HXX

#include <functional>

#include <event.h>

class EventBase {
    struct event_base *event_base;

public:
    EventBase():event_base(::event_init()) {}
    ~EventBase() {
        ::event_base_free(event_base);
    }

    EventBase(const EventBase &other) = delete;
    EventBase &operator=(const EventBase &other) = delete;

    void Dispatch() {
        ::event_base_dispatch(event_base);
    }

    void Break() {
        ::event_base_loopbreak(event_base);
    }
};

class Event {
    struct event event;

    const std::function<void(int fd, short event)> handler;

public:
    Event(std::function<void(int fd, short event)> _handler)
        :handler(_handler) {
        Set(-1, 0);
    }

    ~Event() {
        Delete();
    }

    Event(const Event &other) = delete;
    Event &operator=(const Event &other) = delete;

    void Set(int fd, short mask) {
        ::event_set(&event, fd, mask, Callback, this);
    }

    void Add(const struct timeval *timeout=nullptr) {
        ::event_add(&event, timeout);
    }

    void SetAdd(int fd, short mask, const struct timeval *timeout=nullptr) {
        Set(fd, mask);
        Add(timeout);
    }

    void SetTimer() {
        ::evtimer_set(&event, Callback, this);
    }

    void SetAddTimer(const struct timeval &timeout) {
        SetTimer();
        ::event_add(&event, &timeout);
    }

    void SetSignal(int sig) {
        ::evsignal_set(&event, sig, Callback, this);
    }

    void SetAddSignal(int sig) {
        SetSignal(sig);
        ::evsignal_add(&event, nullptr);
    }

    void Delete() {
        ::event_del(&event);
    }

    bool IsPending(short events) const {
        return ::event_pending(&event, events, nullptr);
    }

    bool IsTimerPending() const {
        return IsPending(EV_TIMEOUT);
    }

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
