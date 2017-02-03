/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_EVENT_HXX
#define SOCKET_EVENT_HXX

#include "Event.hxx"
#include "util/BindMethod.hxx"

class SocketEvent {
    EventLoop &event_loop;

    Event event;

    typedef BoundMethod<void(unsigned events)> Callback;
    const Callback callback;

public:
    SocketEvent(EventLoop &_event_loop, Callback _callback)
        :event_loop(_event_loop), callback(_callback) {}

    SocketEvent(EventLoop &_event_loop, evutil_socket_t fd, unsigned events,
                Callback _callback)
        :SocketEvent(_event_loop, _callback) {
        Set(fd, events);
    }

    EventLoop &GetEventLoop() {
        return event_loop;
    }

    gcc_pure
    evutil_socket_t GetFd() const {
        return event.GetFd();
    }

    gcc_pure
    unsigned GetEvents() const {
        return event.GetEvents();
    }

    void Set(evutil_socket_t fd, unsigned events) {
        event.Set(event_loop, fd, events, EventCallback, this);
    }

    bool Add(const struct timeval *timeout=nullptr) {
        return event.Add(timeout);
    }

    bool Add(const struct timeval &timeout) {
        return event.Add(timeout);
    }

    void Delete() {
        event.Delete();
    }

    gcc_pure
    bool IsPending(unsigned events) const {
        return event.IsPending(events);
    }

    gcc_pure
    bool IsTimerPending() const {
        return event.IsTimerPending();
    }

private:
    static void EventCallback(gcc_unused evutil_socket_t fd, short events,
                              void *ctx) {
        auto &event = *(SocketEvent *)ctx;
        event.callback(events);
    }
};

#endif
