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

#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "pool.hxx"
#include "PInstance.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <stdexcept>

#include <assert.h>

static unsigned num_create, num_fail, num_borrow, num_release, num_destroy;
static bool next_fail;
static bool got_item;
static StockItem *last_item;

struct MyStockItem final : StockItem {
    void *info;

    explicit MyStockItem(CreateStockItem c)
        :StockItem(c) {}

    ~MyStockItem() override {
        ++num_destroy;
    }

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        ++num_borrow;
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        ++num_release;
        return true;
    }
};

/*
 * stock class
 *
 */

static void
my_stock_create(gcc_unused void *ctx, CreateStockItem c,
                void *info,
                gcc_unused struct pool &caller_pool,
                gcc_unused CancellablePointer &cancel_ptr)
{
    auto *item = new MyStockItem(c);

    item->info = info;

    if (next_fail) {
        ++num_fail;
        delete item;
        throw std::runtime_error("next_fail");
    } else {
        ++num_create;
        item->InvokeCreateSuccess();
    }
}

static constexpr StockClass my_stock_class = {
    .create = my_stock_create,
};

class MyStockGetHandler final : public StockGetHandler {
public:
    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override {
        assert(!got_item);

        got_item = true;
        last_item = &item;
    }

    void OnStockItemError(std::exception_ptr ep) override {
        PrintException(ep);

        got_item = true;
        last_item = nullptr;
    }
};

int main(gcc_unused int argc, gcc_unused char **argv)
{
    Stock *stock;
    CancellablePointer cancel_ptr;
    StockItem *item, *second, *third;

    PInstance instance;

    stock = new Stock(instance.event_loop, my_stock_class, nullptr, "test", 3, 8);

    MyStockGetHandler handler;

    struct pool *pool = instance.root_pool;

    /* create first item */

    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(got_item);
    assert(last_item != nullptr);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 0 && num_release == 0 && num_destroy == 0);
    item = last_item;

    /* release first item */

    stock->Put(*item, false);
    instance.event_loop.LoopNonBlock();
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 0 && num_release == 1 && num_destroy == 0);

    /* reuse first item */

    got_item = false;
    last_item = nullptr;
    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(got_item);
    assert(last_item == item);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);

    /* create second item */

    got_item = false;
    last_item = nullptr;
    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(got_item);
    assert(last_item != nullptr);
    assert(last_item != item);
    assert(num_create == 2 && num_fail == 0);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);
    second = last_item;

    /* fail to create third item */

    next_fail = true;
    got_item = false;
    last_item = nullptr;
    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(got_item);
    assert(last_item == nullptr);
    assert(num_create == 2 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);

    /* create third item */

    next_fail = false;
    got_item = false;
    last_item = nullptr;
    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(got_item);
    assert(last_item != nullptr);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);
    third = last_item;

    /* fourth item waiting */

    got_item = false;
    last_item = nullptr;
    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(!got_item);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);

    /* fifth item waiting */

    stock->Get(*pool, nullptr, handler, cancel_ptr);
    assert(!got_item);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);

    /* return third item */

    stock->Put(*third, false);
    instance.event_loop.LoopNonBlock();
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 1);
    assert(got_item);
    assert(last_item == third);

    /* destroy second item */

    got_item = false;
    last_item = nullptr;
    stock->Put(*second, true);
    instance.event_loop.LoopNonBlock();
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 2);
    assert(got_item);
    assert(last_item != nullptr);
    second = last_item;

    /* destroy first item */

    stock->Put(*item, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 3);

    /* destroy second item */

    stock->Put(*second, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 4);

    /* destroy third item */

    stock->Put(*third, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 5);

    /* cleanup */

    delete stock;
}
