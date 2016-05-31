/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SIGNAL_EVENT_HXX
#define SIGNAL_EVENT_HXX

#include "Event.hxx"
#include "util/BindMethod.hxx"

class SignalEvent {
    Event event;

    typedef BoundMethod<void(int)> Callback;
    Callback callback;

public:
    SignalEvent(EventLoop &loop, int sig, Callback _callback)
        :event(loop, sig, EV_SIGNAL|EV_PERSIST, SignalCallback, this),
         callback(_callback) {}

    void Add(const struct timeval *timeout=nullptr) {
        event.Add(timeout);
    }

    void Delete() {
        event.Delete();
    }

private:
    static void SignalCallback(evutil_socket_t fd,
                               gcc_unused short events,
                               void *ctx) {
        auto &event = *(SignalEvent *)ctx;
        event.callback(fd);
    }
};

#endif
