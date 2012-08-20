/*
 * Monitor which attempts to establish a TCP connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_syn_monitor.h"
#include "lb_monitor.h"
#include "lb_config.h"
#include "client-socket.h"
#include "pool.h"

#include <unistd.h>
#include <sys/socket.h>

struct syn_monitor_context {
    const struct lb_monitor_handler *handler;
    void *handler_ctx;
};

/*
 * client_socket handler
 *
 */

static void
syn_monitor_success(int fd, void *ctx)
{
    /* dispose the socket, we don't need it */
    close(fd);

    struct syn_monitor_context *syn = ctx;
    syn->handler->success(syn->handler_ctx);
}

static void
syn_monitor_timeout(void *ctx)
{
    struct syn_monitor_context *syn = ctx;
    syn->handler->timeout(syn->handler_ctx);
}

static void
syn_monitor_error(GError *error, void *ctx)
{
    struct syn_monitor_context *syn = ctx;
    syn->handler->error(error, syn->handler_ctx);
}

static const struct client_socket_handler syn_monitor_handler = {
    .success = syn_monitor_success,
    .timeout = syn_monitor_timeout,
    .error = syn_monitor_error,
};

/*
 * lb_monitor_class
 *
 */

static void
syn_monitor_run(struct pool *pool,
                const struct lb_monitor_config *config,
                const struct sockaddr *address, size_t address_length,
                const struct lb_monitor_handler *handler, void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    struct syn_monitor_context *syn = p_malloc(pool, sizeof(*syn));
    syn->handler = handler;
    syn->handler_ctx = handler_ctx;

    const unsigned timeout = config->timeout > 0
        ? config->timeout
        : 30;

    client_socket_new(pool, address->sa_family, SOCK_STREAM, 0,
                      address, address_length,
                      timeout,
                      &syn_monitor_handler, syn,
                      async_ref);
}

const struct lb_monitor_class syn_monitor_class = {
    .run = syn_monitor_run,
};
