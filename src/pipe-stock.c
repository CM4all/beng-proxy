/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe-stock.h"
#include "stock.h"
#include "fd_util.h"
#include "pool.h"

#include <glib.h>

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

struct pipe_stock_item {
    struct stock_item base;

    int fds[2];
};

static inline bool
valid_fd(int fd)
{
    return fcntl(fd, F_GETFL) >= 0;
}

/*
 * stock class
 *
 */

static struct pool *
pipe_stock_pool(G_GNUC_UNUSED void *ctx, struct pool *parent,
                G_GNUC_UNUSED const char *uri)
{
    return pool_new_linear(parent, "pipe_stock", 64);
}

static void
pipe_stock_create(void *ctx gcc_unused, struct stock_item *_item,
                  G_GNUC_UNUSED const char *uri, G_GNUC_UNUSED void *info,
                  G_GNUC_UNUSED struct pool *caller_pool,
                  G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    int ret;

    ret = pipe_cloexec_nonblock(item->fds);
    if (ret < 0) {
        GError *error = g_error_new(g_file_error_quark(), errno,
                                    "pipe() failed: %s", strerror(errno));
        stock_item_failed(&item->base, error);
        return;
    }

    stock_item_available(&item->base);
}

static bool
pipe_stock_borrow(G_GNUC_UNUSED void *ctx, struct stock_item *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    (void)item;

    assert(valid_fd(item->fds[0]));
    assert(valid_fd(item->fds[1]));

    return true;
}

static void
pipe_stock_release(G_GNUC_UNUSED void *ctx, struct stock_item *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    (void)item;

    assert(valid_fd(item->fds[0]));
    assert(valid_fd(item->fds[1]));
}

static void
pipe_stock_destroy(G_GNUC_UNUSED void *ctx, struct stock_item *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;

    assert(valid_fd(item->fds[0]));
    assert(valid_fd(item->fds[1]));

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
pipe_stock_new(struct pool *pool)
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
