/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

#include "Stock.hxx"
#include "Class.hxx"
#include "GetHandler.hxx"
#include "pool.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"

#include <assert.h>

inline
Stock::Waiting::Waiting(Stock &_stock, struct pool &_pool, void *_info,
                        StockGetHandler &_handler,
                        CancellablePointer &_cancel_ptr)
    :stock(_stock), pool(_pool), info(_info),
     handler(_handler),
     cancel_ptr(_cancel_ptr)
{
    pool_ref(&pool);
    cancel_ptr = *this;
}

inline void
Stock::Waiting::Destroy()
{
    DeleteUnrefPool(pool, this);
}

void
Stock::FadeAll()
{
    for (auto &i : busy)
        i.fade = true;

    ClearIdle();
    ScheduleCheckEmpty();

    // TODO: restart the "num_create" list?
}

/*
 * The "empty()" handler method.
 *
 */

void
Stock::CheckEmpty()
{
    if (IsEmpty() && handler != nullptr)
        handler->OnStockEmpty(*this);
}

void
Stock::ScheduleCheckEmpty()
{
    if (IsEmpty() && handler != nullptr)
        empty_event.Schedule();
}


/*
 * cleanup
 *
 */

void
Stock::CleanupEventCallback()
{
    assert(idle.size() > max_idle);

    /* destroy one third of the idle items */

    for (unsigned i = (idle.size() - max_idle + 2) / 3; i > 0; --i)
        idle.pop_front_and_dispose([this](StockItem *item){
                DestroyItem(*item);
            });

    /* schedule next cleanup */

    if (idle.size() > max_idle)
        ScheduleCleanup();
    else
        CheckEmpty();
}


/*
 * wait operation
 *
 */

inline void
Stock::Waiting::Cancel()
{
    auto &list = stock.waiting;
    const auto i = list.iterator_to(*this);
    list.erase_and_dispose(i, [](Stock::Waiting *w){ w->Destroy(); });
}

void
Stock::RetryWaiting()
{
    if (limit == 0)
        /* no limit configured, no waiters possible */
        return;

    /* first try to serve existing idle items */

    while (!idle.empty()) {
        const auto i = waiting.begin();
        if (i == waiting.end())
            return;

        auto &w = *i;

        waiting.erase(i);

        if (GetIdle(w.handler))
            w.Destroy();
        else
            /* didn't work (probably because borrowing the item has
               failed) - re-add to "waiting" list */
            waiting.push_front(w);
    }

    /* if we're below the limit, create a bunch of new items */

    for (unsigned i = limit - busy.size() - num_create;
         busy.size() + num_create < limit && i > 0 && !waiting.empty();
         --i) {
        auto &w = waiting.front();
        waiting.pop_front();

        GetCreate(w.pool, w.info,
                  w.handler,
                  w.cancel_ptr);
        w.Destroy();
    }
}

void
Stock::ScheduleRetryWaiting()
{
    if (limit > 0 && !waiting.empty() &&
        busy.size() - num_create < limit)
        retry_event.Schedule();
}


/*
 * clear after 60 seconds idle
 *
 */

void
Stock::ClearIdle()
{
    logger.Format(5, "ClearIdle num_idle=%zu num_busy=%zu",
                  idle.size(), busy.size());

    if (idle.size() > max_idle)
        UnscheduleCleanup();

    idle.clear_and_dispose([this](StockItem *item){
            DestroyItem(*item);
        });
}

void
Stock::ClearEventCallback()
{
    logger.Format(6, "ClearEvent may_clear=%d", may_clear);

    if (may_clear)
        ClearIdle();

    may_clear = true;
    ScheduleClear();
    CheckEmpty();
}


/*
 * constructor
 *
 */

Stock::Stock(EventLoop &event_loop, const StockClass &_cls, void *_class_ctx,
             const char *_name, unsigned _limit, unsigned _max_idle,
             StockHandler *_handler)
    :cls(_cls), class_ctx(_class_ctx),
     name(_name),
     limit(_limit), max_idle(_max_idle),
     handler(_handler),
     logger(name),
     retry_event(event_loop, BIND_THIS_METHOD(RetryWaiting)),
     empty_event(event_loop, BIND_THIS_METHOD(CheckEmpty)),
     cleanup_event(event_loop, BIND_THIS_METHOD(CleanupEventCallback)),
     clear_event(event_loop, BIND_THIS_METHOD(ClearEventCallback))
{
    assert(cls.create != nullptr);
    assert(max_idle > 0);

    ScheduleClear();
}

Stock::~Stock()
{
    assert(num_create == 0);

    /* must not delete the Stock when there are busy items left */
    assert(busy.empty());

    retry_event.Cancel();
    empty_event.Cancel();
    cleanup_event.Cancel();
    clear_event.Cancel();

    ClearIdle();
}

void
Stock::DestroyItem(StockItem &item)
{
    item.Destroy(class_ctx);
}

bool
Stock::GetIdle(StockGetHandler &get_handler)
{
    unsigned retry_unclean = idle.size();

    auto i = idle.begin();
    const auto end = idle.end();
    while (i != end) {
        StockItem &item = *i;
        assert(item.is_idle);

        i = idle.erase(i);

        if (item.unclean && retry_unclean > 0) {
            /* postpone reusal of this item until it's "clean" */
            // TODO: replace this kludge
            --retry_unclean;
            idle.push_back(item);
            continue;
        }

        if (idle.size() == max_idle)
            UnscheduleCleanup();

        if (item.Borrow(class_ctx)) {
#ifndef NDEBUG
            item.is_idle = false;
#endif

            busy.push_front(item);

            get_handler.OnStockItemReady(item);
            return true;
        }

        DestroyItem(item);
    }

    ScheduleCheckEmpty();
    return false;
}

void
Stock::GetCreate(struct pool &caller_pool, void *info,
                 StockGetHandler &get_handler,
                 CancellablePointer &cancel_ptr)
{
    ++num_create;

    try {
        cls.create(class_ctx, {*this, get_handler},
                   info, caller_pool, cancel_ptr);
    } catch (...) {
        ItemCreateError(get_handler, std::current_exception());
    }
}

void
Stock::Get(struct pool &caller_pool, void *info,
           StockGetHandler &get_handler,
           CancellablePointer &cancel_ptr)
{
    may_clear = false;

    if (GetIdle(get_handler))
        return;

    if (limit > 0 && busy.size() + num_create >= limit) {
        /* item limit reached: wait for an item to return */
        auto w = NewFromPool<Stock::Waiting>(caller_pool, *this,
                                             caller_pool, info,
                                             get_handler, cancel_ptr);
        waiting.push_front(*w);
        return;
    }

    GetCreate(caller_pool, info, get_handler, cancel_ptr);
}

StockItem *
Stock::GetNow(struct pool &caller_pool, void *info)
{
    struct NowRequest final : public StockGetHandler {
#ifndef NDEBUG
        bool created = false;
#endif
        StockItem *item;
        std::exception_ptr error;

        /* virtual methods from class StockGetHandler */
        void OnStockItemReady(StockItem &_item) override {
#ifndef NDEBUG
            created = true;
#endif

            item = &_item;
        }

        void OnStockItemError(std::exception_ptr ep) override {
#ifndef NDEBUG
            created = true;
#endif

            error = ep;
        }
    };

    NowRequest data;
    CancellablePointer cancel_ptr;

    /* cannot call this on a limited stock */
    assert(limit == 0);

    Get(caller_pool, info, data, cancel_ptr);
    assert(data.created);

    if (data.error)
        std::rethrow_exception(data.error);

    return data.item;
}

void
Stock::ItemCreateSuccess(StockItem &item)
{
    assert(num_create > 0);
    --num_create;

    busy.push_front(item);

    item.handler.OnStockItemReady(item);
}

void
Stock::ItemCreateError(StockItem &item, std::exception_ptr ep)
{
    ItemCreateError(item.handler, ep);
    item.Destroy(class_ctx);
}

void
Stock::ItemCreateAborted(StockItem &item)
{
    ItemCreateAborted();
    item.Destroy(class_ctx);
}

void
Stock::ItemCreateError(StockGetHandler &get_handler, std::exception_ptr ep)
{
    assert(num_create > 0);
    --num_create;

    get_handler.OnStockItemError(ep);

    ScheduleCheckEmpty();
    ScheduleRetryWaiting();
}

void
Stock::ItemCreateAborted()
{
    assert(num_create > 0);
    --num_create;

    ScheduleCheckEmpty();
    ScheduleRetryWaiting();
}

void
Stock::Put(StockItem &item, bool destroy)
{
    assert(!item.is_idle);
    assert(&item.stock == this);

    may_clear = false;

    assert(!busy.empty());

    busy.erase(busy.iterator_to(item));

    if (destroy || item.fade || !item.Release(class_ctx)) {
        DestroyItem(item);
        ScheduleCheckEmpty();
    } else {
#ifndef NDEBUG
        item.is_idle = true;
#endif

        if (idle.size() == max_idle)
            ScheduleCleanup();

        idle.push_front(item);
    }

    ScheduleRetryWaiting();
}

void
Stock::ItemIdleDisconnect(StockItem &item)
{
    assert(item.is_idle);

    assert(!idle.empty());

    idle.erase(idle.iterator_to(item));

    if (idle.size() == max_idle)
        UnscheduleCleanup();

    DestroyItem(item);
    ScheduleCheckEmpty();
}
