/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONNECT_SOCKET_HXX
#define BENG_PROXY_CONNECT_SOCKET_HXX

#include "glibfwd.hxx"

struct pool;
class EventLoop;
class SocketAddress;
class SocketDescriptor;
struct async_operation_ref;

class ConnectSocketHandler {
public:
    virtual void OnSocketConnectSuccess(SocketDescriptor &&fd) = 0;
    virtual void OnSocketConnectTimeout();
    virtual void OnSocketConnectError(GError *error) = 0;
};

/**
 * @param ip_transparent enable the IP_TRANSPARENT option?
 * @param timeout the connect timeout in seconds
 */
void
client_socket_new(EventLoop &event_loop, struct pool &pool,
                  int domain, int type, int protocol,
                  bool ip_transparent,
                  const SocketAddress bind_address,
                  const SocketAddress address,
                  unsigned timeout,
                  ConnectSocketHandler &handler,
                  struct async_operation_ref &async_ref);

#endif
