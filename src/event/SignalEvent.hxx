/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SIGNAL_EVENT_HXX
#define SIGNAL_EVENT_HXX

#include "SocketEvent.hxx"
#include "util/BindMethod.hxx"

#include <assert.h>
#include <signal.h>

class SignalEvent {
    int fd = -1;

    SocketEvent event;

    sigset_t mask;

    typedef BoundMethod<void(int)> Callback;
    const Callback callback;

public:
    SignalEvent(EventLoop &loop, Callback _callback);

    SignalEvent(EventLoop &loop, int signo, Callback _callback)
        :SignalEvent(loop, _callback) {
        Add(signo);
    }

    ~SignalEvent();

    void Add(int signo) {
        assert(fd < 0);

        sigaddset(&mask, signo);
    }

    void Enable();
    void Disable();

private:
    void EventCallback(unsigned events);
};

#endif
