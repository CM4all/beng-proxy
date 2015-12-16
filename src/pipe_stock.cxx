/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe_stock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "system/fd_util.h"
#include "pool.hxx"
#include "gerrno.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

struct PipeStockItem final : PoolStockItem {
    int fds[2];

    explicit PipeStockItem(CreateStockItem c)
        :PoolStockItem(c) {
        fds[0] = -1;
        fds[1] = -1;
    }

    ~PipeStockItem() override {
        if (fds[0] >= 0)
            close(fds[0]);
        if (fds[1] >= 0)
            close(fds[1]);
    }

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override;
    bool Release(gcc_unused void *ctx) override;
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
pipe_stock_pool(gcc_unused void *ctx, struct pool &parent,
                gcc_unused const char *uri)
{
    return pool_new_linear(&parent, "pipe_stock", 128);
}

static void
pipe_stock_create(void *ctx gcc_unused, CreateStockItem c,
                  gcc_unused const char *uri, gcc_unused void *info,
                  gcc_unused struct pool &caller_pool,
                  gcc_unused struct async_operation_ref &async_ref)
{
    auto *item = NewFromPool<PipeStockItem>(c.pool, c);

    int ret = pipe_cloexec_nonblock(item->fds);
    if (ret < 0) {
        GError *error = new_error_errno_msg("pipe() failed");
        item->InvokeCreateError(error);
        return;
    }

    item->InvokeCreateSuccess();
}

bool
PipeStockItem::Borrow(gcc_unused void *ctx)
{
    assert(valid_fd(fds[0]));
    assert(valid_fd(fds[1]));

    return true;
}

bool
PipeStockItem::Release(gcc_unused void *ctx)
{
    assert(valid_fd(fds[0]));
    assert(valid_fd(fds[1]));

    return true;
}

static constexpr StockClass pipe_stock_class = {
    .pool = pipe_stock_pool,
    .create = pipe_stock_create,
};


/*
 * interface
 *
 */

Stock *
pipe_stock_new(struct pool *pool)
{
    return stock_new(*pool, pipe_stock_class, nullptr, nullptr, 0, 64);
}

void
pipe_stock_free(Stock *stock)
{
    stock_free(stock);
}

void
pipe_stock_item_get(StockItem *_item, int fds[2])
{
    auto *item = (PipeStockItem *)_item;

    fds[0] = item->fds[0];
    fds[1] = item->fds[1];
}
