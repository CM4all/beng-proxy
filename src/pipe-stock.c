/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe-stock.h"
#include "stock.h"
#include "fd-util.h"

#include <glib.h>

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct pipe_stock_item {
    struct stock_item base;

    int fds[2];
};

/*
 * stock class
 *
 */

static pool_t
pipe_stock_pool(G_GNUC_UNUSED void *ctx, pool_t parent,
                G_GNUC_UNUSED const char *uri)
{
    return pool_new_linear(parent, "pipe_stock", 64);
}

static void
pipe_stock_create(void *ctx __attr_unused, struct stock_item *_item,
                  G_GNUC_UNUSED const char *uri, G_GNUC_UNUSED void *info,
                  G_GNUC_UNUSED pool_t caller_pool,
                  G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    int ret;

    ret = pipe(item->fds);
    if (ret < 0) {
        daemon_log(1, "pipe() failed: %s\n", strerror(errno));
        stock_item_failed(&item->base);
        return;
    }

    fd_set_cloexec(item->fds[0]);
    fd_set_cloexec(item->fds[1]);

    stock_item_available(&item->base);
}

static bool
pipe_stock_borrow(G_GNUC_UNUSED void *ctx,
                  G_GNUC_UNUSED struct stock_item *item)
{
    return true;
}

static void
pipe_stock_release(G_GNUC_UNUSED void *ctx,
                   G_GNUC_UNUSED struct stock_item *item)
{
}

static void
pipe_stock_destroy(G_GNUC_UNUSED void *ctx, struct stock_item *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;

    close(item->fds[0]);
    close(item->fds[1]);
}

static const struct stock_class pipe_stock_class = {
    .item_size = sizeof(struct pipe_stock_item),
    .pool = pipe_stock_pool,
    .create = pipe_stock_create,
    .borrow = pipe_stock_borrow,
    .release = pipe_stock_release,
    .destroy = pipe_stock_destroy,
};


/*
 * interface
 *
 */

struct stock *
pipe_stock_new(pool_t pool)
{
    return stock_new(pool, &pipe_stock_class, NULL, NULL, 0);
}

void
pipe_stock_item_get(struct stock_item *_item, int fds[2])
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;

    assert(item != NULL);

    fds[0] = item->fds[0];
    fds[1] = item->fds[1];
}
