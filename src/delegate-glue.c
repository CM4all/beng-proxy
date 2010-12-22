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

#include <daemon/log.h>

#include <errno.h>

struct async_operation_ref;
struct hstock;

struct delegate_glue {
    pool_t pool;

    const char *path;

    struct hstock *stock;
    struct stock_item *item;

    delegate_callback_t callback;
    void *callback_ctx;
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
delegate_callback(int fd, void *_ctx)
{
    struct delegate_glue *glue = _ctx;

    glue->callback(fd, glue->callback_ctx);
}

static void
delegate_stock_ready(struct stock_item *item, void *_ctx)
{
    struct delegate_glue *glue = _ctx;

    glue->item = item;

    delegate_open(delegate_stock_item_get(item),
                  &delegate_socket_lease, glue,
                  glue->pool, glue->path,
                  delegate_callback, glue, glue->async_ref);
}

static void
delegate_stock_error(GError *error, void *ctx)
{
    struct delegate_glue *glue = ctx;

    daemon_log(2, "Delegate error: %s\n", error->message);
    g_error_free(error);

    glue->callback(-EINVAL, glue->callback_ctx);
}

static const struct stock_handler delegate_stock_handler = {
    .ready = delegate_stock_ready,
    .error = delegate_stock_error,
};


void
delegate_stock_open(struct hstock *stock, pool_t pool,
                    const char *helper, const char *document_root, bool jail,
                    const char *path,
                    delegate_callback_t callback, void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct delegate_glue *glue = p_malloc(pool, sizeof(*glue));

    glue->pool = pool;
    glue->path = path;
    glue->stock = stock;
    glue->callback = callback;
    glue->callback_ctx = ctx;
    glue->async_ref = async_ref;

    delegate_stock_get(stock, pool, helper, document_root, jail,
                       &delegate_stock_handler, glue, async_ref);
}
