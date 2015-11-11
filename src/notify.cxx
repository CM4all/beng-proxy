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
    notify_callback_t callback;
    void *callback_ctx;

    int fd;

    Event event;

    std::atomic_bool pending;

    Notify()
        :pending(false) {}

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
    auto notify = new Notify();

    notify->callback = callback;
    notify->callback_ctx = ctx;

    notify->fd = eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
    if (notify->fd < 0) {
        set_error_errno_msg(error_r, "eventfd() failed");
        return nullptr;
    }

    notify->event.Set(notify->fd, EV_READ|EV_PERSIST,
                      MakeSimpleEventCallback(Notify, EventFdCallback),
                      notify);
    notify->event.Add();

    return notify;
}

void
notify_free(Notify *notify)
{
    notify->event.Delete();
    close(notify->fd);

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
