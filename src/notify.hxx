/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOTIFY_HXX
#define BENG_PROXY_NOTIFY_HXX

#include "event/SocketEvent.hxx"
#include "util/BindMethod.hxx"

#include <atomic>

class Notify {
    typedef BoundMethod<void()> Callback;
    Callback callback;

    const int fd;
    SocketEvent event;

    std::atomic_bool pending;

public:
    Notify(EventLoop &event_loop, Callback _callback);
    ~Notify();

    void Enable() {
        event.Add();
    }

    void Disable() {
        event.Delete();
    }

    void Signal() {
        if (!pending.exchange(true)) {
            static constexpr uint64_t value = 1;
            (void)write(fd, &value, sizeof(value));
        }
    }

private:
    void EventFdCallback(short events);
};

#endif
