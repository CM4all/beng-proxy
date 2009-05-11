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
};

static void
delegate_socket_release(bool reuse, void *ctx)
{
    struct delegate_glue *glue = ctx;

    hstock_put(glue->stock, glue->path, glue->item, !reuse);
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
delegate_stock_callback(void *_ctx, struct stock_item *item)
{
    struct delegate_glue *glue = _ctx;

    glue->item = item;

    if (item != NULL)
        delegate_open(delegate_stock_item_get(item),
                      &delegate_socket_lease, glue,
                      glue->pool, glue->path,
                      delegate_callback, glue);
    else
        glue->callback(-EINVAL, glue->callback_ctx);
}

void
delegate_stock_open(struct hstock *stock, pool_t pool, const char *path,
                    delegate_callback_t callback, void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct delegate_glue *glue = p_malloc(pool, sizeof(*glue));

    glue->pool = pool;
    glue->path = path;
    glue->stock = stock;
    glue->callback = callback;
    glue->callback_ctx = ctx;

    hstock_get(stock, path, NULL,
               delegate_stock_callback, glue, async_ref);
}
