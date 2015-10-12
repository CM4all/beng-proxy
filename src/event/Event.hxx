/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EVENT_HXX
#define EVENT_HXX

#include <inline/compiler.h>

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

    struct event_base *Get() {
        return event_base;
    }

    void Reinit() {
        event_reinit(event_base);
    }

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

    gcc_pure
    evutil_socket_t GetFd() const {
        return event_get_fd(&event);
    }

    gcc_pure
    short GetEvents() const {
        return event_get_events(&event);
    }

    gcc_pure
    event_callback_fn GetCallback() const {
        return event_get_callback(&event);
    }

    gcc_pure
    void *GetCallbackArg() const {
        return event_get_callback_arg(&event);
    }

    void Set(EventBase &base, evutil_socket_t fd, short mask,
             event_callback_fn callback, void *ctx) {
        ::event_assign(&event, base.Get(), fd, mask, callback, ctx);
    }

    void Set(evutil_socket_t fd, short mask,
             event_callback_fn callback, void *ctx) {
        ::event_set(&event, fd, mask, callback, ctx);
    }

    bool Add(const struct timeval *timeout=nullptr) {
        return ::event_add(&event, timeout) == 0;
    }

    bool Add(const struct timeval &timeout) {
        return Add(&timeout);
    }

    void SetTimer(event_callback_fn callback, void *ctx) {
        ::evtimer_set(&event, callback, ctx);
    }

    void SetSignal(int sig, event_callback_fn callback, void *ctx) {
        ::evsignal_set(&event, sig, callback, ctx);
    }

    void Delete() {
        ::event_del(&event);
    }

    void MakeActive(short events) {
        event_active(&event, events, 0);
    }

    gcc_pure
    bool IsPending(short events) const {
        return ::event_pending(&event, events, nullptr);
    }

    gcc_pure
    bool IsTimerPending() const {
        return IsPending(EV_TIMEOUT);
    }
};

#endif
