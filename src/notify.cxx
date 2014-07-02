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

#include <assert.h>
#include <unistd.h>
#include <glib.h>
#include <event.h>

struct notify {
    notify_callback_t callback;
    void *callback_ctx;

    int fds[2];

    struct event event;

    volatile gint value;
};

static void
notify_event_callback(int fd, gcc_unused short event, void *ctx)
{
    struct notify *notify = (struct notify *)ctx;

    char buffer[32];
    (void)read(fd, buffer, sizeof(buffer));

#if GCC_CHECK_VERSION(4,6) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#endif
    if (g_atomic_int_compare_and_exchange(&notify->value, 1, 0))
#if GCC_CHECK_VERSION(4,6) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        notify->callback(notify->callback_ctx);
}

struct notify *
notify_new(struct pool *pool, notify_callback_t callback, void *ctx,
           GError **error_r)
{
    auto notify = NewFromPool<struct notify>(pool);

    notify->callback = callback;
    notify->callback_ctx = ctx;

    if (pipe_cloexec_nonblock(notify->fds)) {
        set_error_errno_msg(error_r, "pipe() failed");
        return nullptr;
    }

    event_set(&notify->event, notify->fds[0], EV_READ|EV_PERSIST,
              notify_event_callback, notify);
    event_add(&notify->event, nullptr);

    notify->value = 0;
    return notify;
}

void
notify_free(struct notify *notify)
{
    event_del(&notify->event);
    close(notify->fds[0]);
    close(notify->fds[1]);
}

void
notify_signal(struct notify *notify)
{
#if GCC_CHECK_VERSION(4,6) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#endif
    if (g_atomic_int_compare_and_exchange(&notify->value, 0, 1))
#if GCC_CHECK_VERSION(4,6) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        (void)write(notify->fds[1], notify, 1);
}

void
notify_enable(struct notify *notify)
{
    event_add(&notify->event, nullptr);
}

void
notify_disable(struct notify *notify)
{
    event_del(&notify->event);
}
