/*
 * Manage connections to HTTP clients.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONNECTION_HXX
#define BENG_PROXY_CONNECTION_HXX

#include "pool.h"

#include <inline/list.h>

#include <stdint.h>
#include <stddef.h>

struct config;
class SocketDescriptor;
class SocketAddress;

struct client_connection {
    struct list_head siblings;
    struct instance *instance;
    struct pool *pool;
    const struct config *config;
    struct http_server_connection *http;

    /**
     * The name of the site being accessed by the current HTTP
     * request.  This points to memory allocated by the request pool;
     * it is a hack to allow the "log" callback to see this
     * information.
     */
    const char *site_name;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    uint64_t request_start_time;
};

void
new_connection(struct instance *instance,
               SocketDescriptor &&fd, SocketAddress address);

void
close_connection(struct client_connection *connection);

#endif
