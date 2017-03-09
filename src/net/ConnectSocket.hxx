/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONNECT_SOCKET_HXX
#define BENG_PROXY_CONNECT_SOCKET_HXX

#include <exception>

struct pool;
class EventLoop;
class SocketAddress;
class UniqueSocketDescriptor;
class CancellablePointer;

class ConnectSocketHandler {
public:
    virtual void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) = 0;
    virtual void OnSocketConnectTimeout();
    virtual void OnSocketConnectError(std::exception_ptr ep) = 0;
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
                  CancellablePointer &cancel_ptr);

#endif
