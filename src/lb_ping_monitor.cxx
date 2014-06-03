/*
 * Ping (ICMP) monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_ping_monitor.hxx"
#include "lb_monitor.hxx"
#include "ping.hxx"
#include "pool.h"

static void
ping_monitor_response(void *ctx)
{
    LBMonitorHandler &handler = *(LBMonitorHandler *)ctx;
    handler.Success();
}

static void
ping_monitor_timeout(void *ctx)
{
    LBMonitorHandler &handler = *(LBMonitorHandler *)ctx;
    handler.Timeout();
}

static void
ping_monitor_error(GError *error, void *ctx)
{
    LBMonitorHandler &handler = *(LBMonitorHandler *)ctx;
    handler.Error(error);
}

static const struct ping_handler ping_monitor_handler = {
    .response = ping_monitor_response,
    .timeout = ping_monitor_timeout,
    .error = ping_monitor_error,
};

static void
ping_monitor_run(struct pool *pool,
                 G_GNUC_UNUSED const struct lb_monitor_config *config,
                 const struct sockaddr *address, size_t address_length,
                 LBMonitorHandler &handler,
                 struct async_operation_ref *async_ref)
{
    ping(pool, address, address_length,
         &ping_monitor_handler, &handler,
         async_ref);
}

const struct lb_monitor_class ping_monitor_class = {
    .run = ping_monitor_run,
};
