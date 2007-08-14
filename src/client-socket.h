/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CLIENT_SOCKET_H
#define __BENG_CLIENT_SOCKET_H

#include "pool.h"

#include <sys/socket.h>

typedef struct client_socket *client_socket_t;

typedef void (*client_socket_callback_t)(int fd, int err, void *ctx);

int
client_socket_new(pool_t pool,
                  const struct sockaddr *addr, socklen_t addrlen,
                  client_socket_callback_t callback, void *ctx,
                  client_socket_t *client_socket_r);

void
client_socket_free(client_socket_t *client_socket_r);

#endif
