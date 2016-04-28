/*
 * Manage connections to HTTP clients.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include <boost/intrusive/list.hpp>

#include <stdint.h>

struct pool;
struct SslFactory;
struct SslFilter;
struct ThreadSocketFilter;
class SocketDescriptor;
class SocketAddress;
struct HttpServerConnection;
struct LbListenerConfig;
struct LbTcpConnection;

struct LbConnection
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;

    struct lb_instance &instance;

    const LbListenerConfig &listener;

    /**
     * The client's address formatted as a string (for logging).  This
     * is guaranteed to be non-nullptr.
     */
    const char *client_address;

    SslFilter *ssl_filter = nullptr;
    ThreadSocketFilter *thread_socket_filter = nullptr;

    HttpServerConnection *http;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    uint64_t request_start_time;

    LbTcpConnection *tcp;

    LbConnection(struct pool &_pool, struct lb_instance &_instance,
                 const LbListenerConfig &_listener,
                 SocketAddress _client_address);
};

LbConnection *
lb_connection_new(struct lb_instance *instance,
                  const LbListenerConfig *listener,
                  SslFactory *ssl_factory,
                  SocketDescriptor &&fd, SocketAddress address);

void
lb_connection_remove(LbConnection *connection);

void
lb_connection_close(LbConnection *connection);

#endif
