/*
 * Ping (ICMP) monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_ping_monitor.h"
#include "lb_monitor.h"
#include "ping.h"
#include "pool.h"

struct ping_monitor_context {
    const struct lb_monitor_handler *handler;
    void *handler_ctx;
};

static void
ping_monitor_response(void *ctx)
{
    struct ping_monitor_context *p = ctx;
    p->handler->success(p->handler_ctx);
}

static void
ping_monitor_timeout(void *ctx)
{
    struct ping_monitor_context *p = ctx;
    p->handler->timeout(p->handler_ctx);
}

static void
ping_monitor_error(GError *error, void *ctx)
{
    struct ping_monitor_context *p = ctx;
    p->handler->error(error, p->handler_ctx);
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
                 const struct lb_monitor_handler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    struct ping_monitor_context *p = p_malloc(pool, sizeof(*p));
    p->handler = handler;
    p->handler_ctx = handler_ctx;

    ping(pool, address, address_length,
         &ping_monitor_handler, p,
         async_ref);
}

const struct lb_monitor_class ping_monitor_class = {
    .run = ping_monitor_run,
};
