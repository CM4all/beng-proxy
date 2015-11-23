/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ShutdownListener.hxx"
#include "Callback.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <signal.h>
#include <unistd.h>

inline void
ShutdownListener::SignalCallback(evutil_socket_t fd, gcc_unused short events)
{
    daemon_log(2, "caught signal %d, shutting down (pid=%d)\n",
               (int)fd, (int)getpid());

    shutdown_listener_deinit(this);
    callback(callback_ctx);
}

ShutdownListener::ShutdownListener(void (*_callback)(void *ctx), void *_ctx)
    :callback(_callback), callback_ctx(_ctx)
{
    event_set(&sigterm_event, SIGTERM, EV_SIGNAL,
              MakeEventCallback(ShutdownListener, SignalCallback), this);
    event_set(&sigint_event, SIGINT, EV_SIGNAL,
              MakeEventCallback(ShutdownListener, SignalCallback), this);
    event_set(&sigquit_event, SIGQUIT, EV_SIGNAL,
              MakeEventCallback(ShutdownListener, SignalCallback), this);
}

void
shutdown_listener_init(ShutdownListener *l)
{
    event_add(&l->sigterm_event, NULL);
    event_add(&l->sigint_event, NULL);
    event_add(&l->sigquit_event, NULL);
}

void
shutdown_listener_deinit(ShutdownListener *l)
{
    event_del(&l->sigterm_event);
    event_del(&l->sigint_event);
    event_del(&l->sigquit_event);
}

