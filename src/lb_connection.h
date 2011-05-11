/*
 * Manage connections to HTTP clients.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include <inline/list.h>

#include <stdint.h>
#include <stddef.h>

struct pool;
struct sockaddr;

struct lb_connection {
    struct list_head siblings;

    struct pool *pool;

    struct lb_instance *instance;

    const struct lb_listener_config *listener;

    const struct config *config;
    struct http_server_connection *http;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    uint64_t request_start_time;
};

struct lb_connection *
connection_new(struct lb_instance *instance,
               const struct lb_listener_config *listener,
               int fd, const struct sockaddr *addr, size_t addrlen);

void
connection_close(struct lb_connection *connection);

#endif
