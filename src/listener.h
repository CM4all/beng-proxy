/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LISTENER_H
#define __BENG_LISTENER_H

#include <stddef.h>

struct pool;
struct sockaddr;
struct listener;

typedef void (*listener_callback_t)(int fd, const struct sockaddr *address,
                                    size_t length, void *ctx);

struct listener *
listener_new(struct pool *pool, int family, int socktype, int protocol,
             const struct sockaddr *address, size_t address_length,
             listener_callback_t callback, void *ctx);

int
listener_tcp_port_new(struct pool *pool, int port,
                      listener_callback_t callback, void *ctx,
                      struct listener **listener_r);

void
listener_free(struct listener **listener_r);

void
listener_event_add(struct listener *listener);

void
listener_event_del(struct listener *listener);

#endif
