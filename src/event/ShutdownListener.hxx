/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SHUTDOWN_LISTENER_HXX
#define BENG_PROXY_SHUTDOWN_LISTENER_HXX

#include <event.h>

struct ShutdownListener {
    struct event sigterm_event, sigint_event, sigquit_event;

    void (*callback)(void *ctx);
    void *callback_ctx;

    void SignalCallback(evutil_socket_t fd, short events);
};

void
shutdown_listener_init(ShutdownListener *l,
                       void (*callback)(void *ctx), void *ctx);

void
shutdown_listener_deinit(ShutdownListener *l);

#endif
