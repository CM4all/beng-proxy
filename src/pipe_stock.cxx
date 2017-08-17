/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pipe_stock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

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

    if (!UniqueFileDescriptor::CreatePipeNonBlock(item->fds[0],
                                                  item->fds[1])) {
        int e = errno;
        delete item;
        throw MakeErrno(e, "pipe() failed");
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
