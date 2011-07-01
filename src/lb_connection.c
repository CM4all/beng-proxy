/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_connection.h"
#include "lb_instance.h"
#include "lb_http.h"
#include "strmap.h"
#include "http-server.h"
#include "address.h"
#include "drop.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * public
 *
 */

struct lb_connection *
lb_connection_new(struct lb_instance *instance,
                  const struct lb_listener_config *listener,
                  int fd, const struct sockaddr *addr, size_t addrlen)
{
    struct lb_connection *connection;

    /* determine the local socket address */
    struct sockaddr_storage local_address;
    socklen_t local_address_length = sizeof(local_address);
    if (getsockname(fd, (struct sockaddr *)&local_address,
                    &local_address_length) < 0)
        local_address_length = 0;

    struct pool *pool = pool_new_linear(instance->pool, "client_connection",
                                        16384);
    pool_set_major(pool);

    connection = p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->instance = instance;
    connection->listener = listener;

    list_add(&connection->siblings, &instance->connections);
    ++connection->instance->num_connections;

    http_server_connection_new(pool, fd, ISTREAM_TCP,
                               local_address_length > 0
                               ? (const struct sockaddr *)&local_address
                               : NULL,
                               local_address_length,
                               address_to_string(pool, addr, addrlen),
                               &lb_http_connection_handler,
                               connection,
                               &connection->http);
    return connection;
}

void
lb_connection_close(struct lb_connection *connection)
{
    assert(connection->http != NULL);

    http_server_connection_close(connection->http);
}
