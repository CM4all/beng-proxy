/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "notify.h"
#include "fd_util.h"
#include "pool.h"

#include <inline/compiler.h>

#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
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
notify_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct notify *notify = ctx;

    char buffer[32];
    read(fd, buffer, sizeof(buffer));

#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#endif
    if (g_atomic_int_compare_and_exchange(&notify->value, 1, 0))
#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic pop
#endif
        notify->callback(notify->callback_ctx);
}

struct notify *
notify_new(struct pool *pool, notify_callback_t callback, void *ctx,
           GError **error_r)
{
    struct notify *notify = p_malloc(pool, sizeof(*notify));

    notify->callback = callback;
    notify->callback_ctx = ctx;

    if (pipe_cloexec_nonblock(notify->fds)) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "pipe() failed: %s", strerror(errno));
        return NULL;
    }

    event_set(&notify->event, notify->fds[0], EV_READ|EV_PERSIST,
              notify_event_callback, notify);
    event_add(&notify->event, NULL);

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
#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#endif
    if (g_atomic_int_compare_and_exchange(&notify->value, 0, 1))
#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic pop
#endif
        write(notify->fds[1], notify, 1);
}
