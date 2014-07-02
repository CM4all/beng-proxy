/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "notify.hxx"
#include "fd_util.h"
#include "pool.h"
#include "gerrno.h"

#include <inline/compiler.h>

#include <event.h>

#include <atomic>

#include <assert.h>
#include <unistd.h>

class Notify {
public:
    notify_callback_t callback;
    void *callback_ctx;

    int fds[2];

    struct event event;

    std::atomic_bool pending;

    Notify()
        :pending(false) {}
};

static void
notify_event_callback(int fd, gcc_unused short event, void *ctx)
{
    Notify *notify = (Notify *)ctx;

    char buffer[32];
    (void)read(fd, buffer, sizeof(buffer));

    if (notify->pending.exchange(false))
        notify->callback(notify->callback_ctx);
}

Notify *
notify_new(struct pool *pool, notify_callback_t callback, void *ctx,
           GError **error_r)
{
    auto notify = NewFromPool<Notify>(pool);

    notify->callback = callback;
    notify->callback_ctx = ctx;

    if (pipe_cloexec_nonblock(notify->fds)) {
        set_error_errno_msg(error_r, "pipe() failed");
        return nullptr;
    }

    event_set(&notify->event, notify->fds[0], EV_READ|EV_PERSIST,
              notify_event_callback, notify);
    event_add(&notify->event, nullptr);

    return notify;
}

void
notify_free(Notify *notify)
{
    event_del(&notify->event);
    close(notify->fds[0]);
    close(notify->fds[1]);
}

void
notify_signal(Notify *notify)
{
    if (!notify->pending.exchange(true))
        (void)write(notify->fds[1], notify, 1);
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
