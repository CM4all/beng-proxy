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
struct ssl_factory;
struct SslFilter;
class SocketAddress;

struct lb_connection
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool *pool;

    struct lb_instance *instance;

    const struct lb_listener_config *listener;

    const struct config *config;
    SslFilter *ssl_filter;
    struct http_server_connection *http;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    uint64_t request_start_time;

    struct lb_tcp *tcp;
};

struct lb_connection *
lb_connection_new(struct lb_instance *instance,
                  const struct lb_listener_config *listener,
                  struct ssl_factory *ssl_factory,
                  int fd, SocketAddress address);

void
lb_connection_remove(struct lb_connection *connection);

void
lb_connection_close(struct lb_connection *connection);

#endif
