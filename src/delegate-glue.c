/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-glue.h"
#include "delegate-stock.h"
#include "stock.h"
#include "lease.h"
#include "pool.h"

#include <daemon/log.h>

#include <errno.h>

struct async_operation_ref;
struct hstock;

struct delegate_glue {
    struct pool *pool;

    const char *path;

    struct hstock *stock;
    struct stock_item *item;

    const struct delegate_handler *handler;
    void *handler_ctx;
    struct async_operation_ref *async_ref;
};

static void
delegate_socket_release(bool reuse, void *ctx)
{
    struct delegate_glue *glue = ctx;

    delegate_stock_put(glue->stock, glue->item, !reuse);
}

static const struct lease delegate_socket_lease = {
    .release = delegate_socket_release,
};

static void
delegate_stock_ready(struct stock_item *item, void *_ctx)
{
    struct delegate_glue *glue = _ctx;

    glue->item = item;

    delegate_open(delegate_stock_item_get(item),
                  &delegate_socket_lease, glue,
                  glue->pool, glue->path,
                  glue->handler, glue->handler_ctx, glue->async_ref);
}

static void
delegate_stock_error(GError *error, void *ctx)
{
    struct delegate_glue *glue = ctx;

    glue->handler->error(error, glue->handler_ctx);
}

static const struct stock_get_handler delegate_stock_handler = {
    .ready = delegate_stock_ready,
    .error = delegate_stock_error,
};


void
delegate_stock_open(struct hstock *stock, struct pool *pool,
                    const char *helper,
                    const struct jail_params *jail,
                    const char *path,
                    const struct delegate_handler *handler, void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct delegate_glue *glue = p_malloc(pool, sizeof(*glue));

    glue->pool = pool;
    glue->path = path;
    glue->stock = stock;
    glue->handler = handler;
    glue->handler_ctx = ctx;
    glue->async_ref = async_ref;

    delegate_stock_get(stock, pool, helper, jail,
                       &delegate_stock_handler, glue, async_ref);
}
