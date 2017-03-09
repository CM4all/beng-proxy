/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe_stock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "gerrno.h"

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

struct PipeStockItem final : HeapStockItem {
    UniqueFileDescriptor fds[2];

    explicit PipeStockItem(CreateStockItem c)
        :HeapStockItem(c) {
    }

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override;
    bool Release(gcc_unused void *ctx) override;
};

/*
 * stock class
 *
 */

static void
pipe_stock_create(gcc_unused void *ctx,
                  CreateStockItem c,
                  gcc_unused void *info,
                  gcc_unused struct pool &caller_pool,
                  gcc_unused CancellablePointer &cancel_ptr)
{
    auto *item = new PipeStockItem(c);

    int ret = UniqueFileDescriptor::CreatePipeNonBlock(item->fds[0],
                                                       item->fds[1]);
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
    assert(fds[0].IsValid());
    assert(fds[1].IsValid());

    return true;
}

bool
PipeStockItem::Release(gcc_unused void *ctx)
{
    assert(fds[0].IsValid());
    assert(fds[1].IsValid());

    return true;
}

static constexpr StockClass pipe_stock_class = {
    .create = pipe_stock_create,
};


/*
 * interface
 *
 */

Stock *
pipe_stock_new(EventLoop &event_loop)
{
    return new Stock(event_loop, pipe_stock_class, nullptr, "pipe", 0, 64);
}

void
pipe_stock_free(Stock *stock)
{
    delete stock;
}

void
pipe_stock_item_get(StockItem *_item, FileDescriptor fds[2])
{
    auto *item = (PipeStockItem *)_item;

    fds[0] = item->fds[0].ToFileDescriptor();
    fds[1] = item->fds[1].ToFileDescriptor();
}
