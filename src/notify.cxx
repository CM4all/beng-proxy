/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "notify.hxx"
#include "system/Error.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"

#include <inline/compiler.h>

#include <atomic>

#include <unistd.h>
#include <sys/eventfd.h>

class Notify {
    const notify_callback_t callback;
    void *const callback_ctx;

    const int fd;

    Event event;

    std::atomic_bool pending;

public:
    Notify(int _fd, notify_callback_t _callback, void *_ctx)
        :callback(_callback), callback_ctx(_ctx),
         fd(_fd),
         event(fd, EV_READ|EV_PERSIST,
               MakeSimpleEventCallback(Notify, EventFdCallback),
               this),
         pending(false) {
        event.Add();
    }

    ~Notify() {
        event.Delete();
        close(fd);
    }

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

inline void
Notify::EventFdCallback()
{
    uint64_t value;
    (void)read(fd, &value, sizeof(value));

    if (pending.exchange(false))
        callback(callback_ctx);
}

Notify *
notify_new(notify_callback_t callback, void *ctx)
{
    int fd = eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
    if (fd < 0)
        throw MakeErrno("eventfd() failed");

    return new Notify(fd, callback, ctx);
}

void
notify_free(Notify *notify)
{
    delete notify;
}

void
notify_signal(Notify *notify)
{
    notify->Signal();
}

void
notify_enable(Notify *notify)
{
    notify->Enable();
}

void
notify_disable(Notify *notify)
{
    notify->Disable();
}
