/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SHUTDOWN_LISTENER_H
#define BENG_PROXY_SHUTDOWN_LISTENER_H

#include <event.h>

struct shutdown_listener {
    struct event sigterm_event, sigint_event, sigquit_event;

    void (*callback)(void *ctx);
    void *callback_ctx;
};

#ifdef __cplusplus
extern "C" {
#endif

void
shutdown_listener_init(struct shutdown_listener *l,
                       void (*callback)(void *ctx), void *ctx);

void
shutdown_listener_deinit(struct shutdown_listener *l);

#ifdef __cplusplus
}
#endif

#endif
