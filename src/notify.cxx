/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "notify.hxx"
#include "fd_util.h"
#include "gerrno.h"

#include <inline/compiler.h>

#include <event.h>

#include <atomic>

#include <assert.h>
#include <unistd.h>
#include <sys/eventfd.h>

class Notify {
public:
    notify_callback_t callback;
    void *callback_ctx;

    int fd;

    struct event event;

    std::atomic_bool pending;

    Notify()
        :pending(false) {}
};

static void
notify_event_callback(int fd, gcc_unused short event, void *ctx)
{
    Notify *notify = (Notify *)ctx;

    uint64_t value;
    (void)read(fd, &value, sizeof(value));

    if (notify->pending.exchange(false))
        notify->callback(notify->callback_ctx);
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

    event_set(&notify->event, notify->fd, EV_READ|EV_PERSIST,
              notify_event_callback, notify);
    event_add(&notify->event, nullptr);

    return notify;
}

void
notify_free(Notify *notify)
{
    event_del(&notify->event);
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
    event_add(&notify->event, nullptr);
}

void
notify_disable(Notify *notify)
{
    event_del(&notify->event);
}
