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

    void (*const callback)(void *ctx);
    void *const callback_ctx;

    ShutdownListener(void (*_callback)(void *ctx), void *_ctx);

    ShutdownListener(const ShutdownListener &) = delete;
    ShutdownListener &operator=(const ShutdownListener &) = delete;

    void SignalCallback(evutil_socket_t fd, short events);
};

void
shutdown_listener_init(ShutdownListener *l);

void
shutdown_listener_deinit(ShutdownListener *l);

#endif
