/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOTIFY_HXX
#define BENG_PROXY_NOTIFY_HXX

#include "event/Event.hxx"
#include "util/BindMethod.hxx"

#include <atomic>

class Notify {
    typedef BoundMethod<void()> Callback;
    Callback callback;

    const int fd;
    Event event;

    std::atomic_bool pending;

public:
    explicit Notify(Callback _callback);
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
    void EventFdCallback();
};

#endif
