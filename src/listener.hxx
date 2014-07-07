/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LISTENER_HXX
#define BENG_PROXY_LISTENER_HXX

class SocketAddress;
class Error;

struct listener_handler {
    void (*connected)(int fd, SocketAddress address, void *ctx);
    void (*error)(Error &&error, void *ctx);
};

struct listener *
listener_new(int family, int socktype, int protocol,
             SocketAddress address,
             const struct listener_handler *handler, void *ctx,
             Error &error);

struct listener *
listener_tcp_port_new(int port,
                      const struct listener_handler *handler, void *ctx,
                      Error &error);

void
listener_free(struct listener **listener_r);

void
listener_event_add(struct listener *listener);

void
listener_event_del(struct listener *listener);

#endif
