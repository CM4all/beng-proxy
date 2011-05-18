/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CLIENT_SOCKET_H
#define __BENG_CLIENT_SOCKET_H

#include <stddef.h>

struct pool;
struct sockaddr;
struct async_operation_ref;

typedef void (*client_socket_callback_t)(int fd, int err, void *ctx);

void
client_socket_new(struct pool *pool,
                  int domain, int type, int protocol,
                  const struct sockaddr *addr, size_t addrlen,
                  client_socket_callback_t callback, void *ctx,
                  struct async_operation_ref *async_ref);

#endif
