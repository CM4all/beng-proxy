/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "notify.hxx"
#include "gerrno.h"
#include "event/Event.hxx"
#include "event/Callback.hxx"

#include <inline/compiler.h>

#include <atomic>

#include <unistd.h>
#include <sys/eventfd.h>

class Notify {
public:
    const notify_callback_t callback;
    void *const callback_ctx;

    const int fd;

    Event event;

    std::atomic_bool pending;

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
notify_new(notify_callback_t callback, void *ctx, GError **error_r)
{
    int fd = eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
    if (fd < 0) {
        set_error_errno_msg(error_r, "eventfd() failed");
        return nullptr;
    }

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
    if (!notify->pending.exchange(true)) {
        static constexpr uint64_t value = 1;
        (void)write(notify->fd, &value, sizeof(value));
    }
}

void
notify_enable(Notify *notify)
{
    notify->event.Add();
}

void
notify_disable(Notify *notify)
{
    notify->event.Delete();
}
