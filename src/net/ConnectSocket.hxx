/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONNECT_SOCKET_HXX
#define BENG_PROXY_CONNECT_SOCKET_HXX

#include "glibfwd.hxx"

#include <stddef.h>

struct pool;
struct sockaddr;
struct async_operation_ref;

struct ConnectSocketHandler {
    void (*success)(int fd, void *ctx);
    void (*timeout)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

/**
 * @param ip_transparent enable the IP_TRANSPARENT option?
 * @param timeout the connect timeout in seconds
 */
void
client_socket_new(struct pool &pool,
                  int domain, int type, int protocol,
                  bool ip_transparent,
                  const struct sockaddr *bind_addr, size_t bind_addrlen,
                  const struct sockaddr *addr, size_t addrlen,
                  unsigned timeout,
                  const ConnectSocketHandler &handler, void *ctx,
                  struct async_operation_ref &async_ref);

#endif
