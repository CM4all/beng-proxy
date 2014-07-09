/*
 * Monitor which attempts to establish a TCP connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_syn_monitor.hxx"
#include "lb_monitor.hxx"
#include "lb_config.hxx"
#include "pool.h"
#include "net/ConnectSocket.hxx"

#include <unistd.h>
#include <sys/socket.h>

/*
 * client_socket handler
 *
 */

static void
syn_monitor_success(int fd, void *ctx)
{
    /* dispose the socket, we don't need it */
    close(fd);

    LBMonitorHandler &handler = *(LBMonitorHandler *)ctx;
    handler.Success();
}

static void
syn_monitor_timeout(void *ctx)
{
    LBMonitorHandler &handler = *(LBMonitorHandler *)ctx;
    handler.Timeout();
}

static void
syn_monitor_error(GError *error, void *ctx)
{
    LBMonitorHandler &handler = *(LBMonitorHandler *)ctx;
    handler.Error(error);
}

static constexpr ConnectSocketHandler syn_monitor_handler = {
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
                LBMonitorHandler &handler,
                struct async_operation_ref *async_ref)
{
    const unsigned timeout = config->timeout > 0
        ? config->timeout
        : 30;

    client_socket_new(*pool, address->sa_family, SOCK_STREAM, 0,
                      false,
                      nullptr, 0,
                      address, address_length,
                      timeout,
                      syn_monitor_handler, &handler,
                      *async_ref);
}

const struct lb_monitor_class syn_monitor_class = {
    .run = syn_monitor_run,
};
