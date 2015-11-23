/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "shutdown_listener.h"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <signal.h>
#include <unistd.h>

static void
shutdown_event_callback(gcc_unused int fd, gcc_unused short event, void *ctx)
{
    struct shutdown_listener *l = ctx;

    daemon_log(2, "caught signal %d, shutting down (pid=%d)\n",
               fd, (int)getpid());

    shutdown_listener_deinit(l);
    l->callback(l->callback_ctx);
}

void
shutdown_listener_init(struct shutdown_listener *l,
                       void (*callback)(void *ctx), void *ctx)
{
    event_set(&l->sigterm_event, SIGTERM, EV_SIGNAL,
              shutdown_event_callback, l);
    event_add(&l->sigterm_event, NULL);

    event_set(&l->sigint_event, SIGINT, EV_SIGNAL,
              shutdown_event_callback, l);
    event_add(&l->sigint_event, NULL);

    event_set(&l->sigquit_event, SIGQUIT, EV_SIGNAL,
              shutdown_event_callback, l);
    event_add(&l->sigquit_event, NULL);

    l->callback = callback;
    l->callback_ctx = ctx;
}

void
shutdown_listener_deinit(struct shutdown_listener *l)
{
    event_del(&l->sigterm_event);
    event_del(&l->sigint_event);
    event_del(&l->sigquit_event);
}

