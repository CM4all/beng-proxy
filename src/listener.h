/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LISTENER_H
#define __BENG_LISTENER_H

#include <glib.h>
#include <stddef.h>

struct pool;
struct sockaddr;
struct listener;

struct listener_handler {
    void (*connected)(int fd, const struct sockaddr *address,
                      size_t length, void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct listener *
listener_new(struct pool *pool, int family, int socktype, int protocol,
             const struct sockaddr *address, size_t address_length,
             const struct listener_handler *handler, void *ctx,
             GError **error_r);

struct listener *
listener_tcp_port_new(struct pool *pool, int port,
                      const struct listener_handler *handler, void *ctx,
                      GError **error_r);

void
listener_free(struct listener **listener_r);

void
listener_event_add(struct listener *listener);

void
listener_event_del(struct listener *listener);

#endif
