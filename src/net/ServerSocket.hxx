/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SERVER_SOCKET_HXX
#define SERVER_SOCKET_HXX

class ServerSocket;
class SocketDescriptor;
class SocketAddress;
class Error;

struct listener_handler {
    void (*connected)(SocketDescriptor &&fd, SocketAddress address, void *ctx);
    void (*error)(Error &&error, void *ctx);
};

ServerSocket *
listener_new(int family, int socktype, int protocol,
             SocketAddress address,
             const struct listener_handler *handler, void *ctx,
             Error &error);

ServerSocket *
listener_tcp_port_new(int port,
                      const struct listener_handler *handler, void *ctx,
                      Error &error);

void
listener_free(ServerSocket **listener_r);

void
listener_event_add(ServerSocket *listener);

void
listener_event_del(ServerSocket *listener);

#endif
