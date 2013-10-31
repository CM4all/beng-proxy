/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CLIENT_SOCKET_H
#define __BENG_CLIENT_SOCKET_H

#include <glib.h>
#include <stddef.h>

struct pool;
struct sockaddr;
struct async_operation_ref;

struct client_socket_handler {
    void (*success)(int fd, void *ctx);
    void (*timeout)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param timeout the connect timeout in seconds
 */
void
client_socket_new(struct pool *pool,
                  int domain, int type, int protocol,
                  const struct sockaddr *bind_addr, size_t bind_addrlen,
                  const struct sockaddr *addr, size_t addrlen,
                  unsigned timeout,
                  const struct client_socket_handler *handler, void *ctx,
                  struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
