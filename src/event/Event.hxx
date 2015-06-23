/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EVENT_HXX
#define EVENT_HXX

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

public:
    Event() = default;

    Event(const Event &other) = delete;
    Event &operator=(const Event &other) = delete;

    void Set(evutil_socket_t fd, short mask,
             void (*callback)(evutil_socket_t, short, void *),
             void *ctx) {
        ::event_set(&event, fd, mask, callback, ctx);
    }

    void Add(const struct timeval *timeout=nullptr) {
        ::event_add(&event, timeout);
    }

    void SetTimer(void (*callback)(evutil_socket_t, short, void *),
                  void *ctx) {
        ::evtimer_set(&event, callback, ctx);
    }

    void SetSignal(int sig,
                   void (*callback)(evutil_socket_t, short, void *),
                   void *ctx) {
        ::evsignal_set(&event, sig, callback, ctx);
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
};

#endif
