/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LISTENER_H
#define __BENG_LISTENER_H

#include "pool.h"

#include <sys/socket.h>

typedef struct listener *listener_t;

typedef void (*listener_callback_t)(int fd,
                                    const struct sockaddr *addr, socklen_t addrlen,
                                    void *ctx);

int
listener_tcp_port_new(pool_t pool, int port,
                      listener_callback_t callback, void *ctx,
                      listener_t *listener_r);

void
listener_free(listener_t *listener_r);

#endif
