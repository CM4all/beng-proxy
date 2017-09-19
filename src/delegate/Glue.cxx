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

#include "Glue.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "Stock.hxx"
#include "stock/Item.hxx"
#include "stock/MapStock.hxx"
#include "lease.hxx"
#include "pool.hxx"

struct DelegateGlue final : Lease {
    StockItem &item;

    explicit DelegateGlue(StockItem &_item):item(_item) {}

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        item.Put(!reuse);
    }
};

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions &options,
                    const char *path,
                    DelegateHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    StockItem *item;

    try {
        item = delegate_stock_get(stock, helper, options);
    } catch (...) {
        handler.OnDelegateError(std::current_exception());
        return;
    }

    auto glue = NewFromPool<DelegateGlue>(*pool, *item);
    delegate_open(stock->GetEventLoop(), delegate_stock_item_get(*item), *glue,
                  pool, path,
                  handler, cancel_ptr);
}
