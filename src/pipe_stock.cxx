/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe_stock.hxx"
#include "stock.hxx"
#include "fd_util.h"
#include "pool.hxx"
#include "gerrno.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

struct pipe_stock_item {
    StockItem base;

    int fds[2];
};

#ifndef NDEBUG

static inline bool
valid_fd(int fd)
{
    return fcntl(fd, F_GETFL) >= 0;
}

#endif

/*
 * stock class
 *
 */

static struct pool *
pipe_stock_pool(gcc_unused void *ctx, struct pool *parent,
                gcc_unused const char *uri)
{
    return pool_new_linear(parent, "pipe_stock", 128);
}

static void
pipe_stock_create(void *ctx gcc_unused, StockItem *_item,
                  gcc_unused const char *uri, gcc_unused void *info,
                  gcc_unused struct pool *caller_pool,
                  gcc_unused struct async_operation_ref *async_ref)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    int ret;

    ret = pipe_cloexec_nonblock(item->fds);
    if (ret < 0) {
        GError *error = new_error_errno_msg("pipe() failed");
        stock_item_failed(&item->base, error);
        return;
    }

    stock_item_available(&item->base);
}

static bool
pipe_stock_borrow(gcc_unused void *ctx, StockItem *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    (void)item;

    assert(valid_fd(item->fds[0]));
    assert(valid_fd(item->fds[1]));

    return true;
}

static void
pipe_stock_release(gcc_unused void *ctx, StockItem *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;
    (void)item;

    assert(valid_fd(item->fds[0]));
    assert(valid_fd(item->fds[1]));
}

static void
pipe_stock_destroy(gcc_unused void *ctx, StockItem *_item)
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;

    assert(valid_fd(item->fds[0]));
    assert(valid_fd(item->fds[1]));

    close(item->fds[0]);
    close(item->fds[1]);
}

static constexpr StockClass pipe_stock_class = {
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

Stock *
pipe_stock_new(struct pool *pool)
{
    return stock_new(pool, &pipe_stock_class, nullptr, nullptr, 0, 64, nullptr, nullptr);
}

void
pipe_stock_item_get(StockItem *_item, int fds[2])
{
    struct pipe_stock_item *item = (struct pipe_stock_item *)_item;

    assert(item != nullptr);

    fds[0] = item->fds[0];
    fds[1] = item->fds[1];
}
