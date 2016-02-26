/*
 * Manage connections to HTTP clients.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONNECTION_HXX
#define BENG_PROXY_CONNECTION_HXX

#include "pool.hxx"

#include <inline/list.h>

#include <stdint.h>
#include <stddef.h>

struct BpConfig;
struct BpInstance;
class SocketDescriptor;
class SocketAddress;
struct HttpServerConnection;

struct BpConnection {
    struct list_head siblings;
    BpInstance *const instance;
    struct pool *const pool;
    const BpConfig *const config;

    const char *const listener_tag;

    HttpServerConnection *http;

    /**
     * The name of the site being accessed by the current HTTP
     * request.  This points to memory allocated by the request pool;
     * it is a hack to allow the "log" callback to see this
     * information.
     */
    const char *site_name = nullptr;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    uint64_t request_start_time;

    BpConnection(BpInstance &_instance, struct pool &_pool,
                 const char *_listener_tag);
};

void
new_connection(BpInstance *instance,
               SocketDescriptor &&fd, SocketAddress address,
               const char *listener_tag);

void
close_connection(BpConnection *connection);

#endif
