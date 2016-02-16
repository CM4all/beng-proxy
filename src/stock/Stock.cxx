/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "Class.hxx"
#include "GetHandler.hxx"
#include "pool.hxx"
#include "event/Callback.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>

inline
Stock::Waiting::Waiting(Stock &_stock, struct pool &_pool, void *_info,
                        StockGetHandler &_handler,
                        struct async_operation_ref &_async_ref)
    :stock(_stock), pool(_pool), info(_info),
     handler(_handler),
     async_ref(_async_ref)
{
    operation.Init2<Waiting>();
    pool_ref(&pool);
    async_ref.Set(operation);
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
        empty_event.Add();
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
Stock::Waiting::Abort()
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

        w.operation.Finished();
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

        w.operation.Finished();
        GetCreate(w.pool, w.info,
                  w.handler,
                  w.async_ref);
        w.Destroy();
    }
}

void
Stock::ScheduleRetryWaiting()
{
    if (limit > 0 && !waiting.empty() &&
        busy.size() - num_create < limit)
        retry_event.Add();
}


/*
 * clear after 60 seconds idle
 *
 */

void
Stock::ClearIdle()
{
    daemon_log(5, "Stock::ClearIdle(%p, '%s') num_idle=%zu num_busy=%zu\n",
               (const void *)this, name.c_str(),
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
    daemon_log(6, "Stock::ClearEvent(%p, '%s') may_clear=%d\n",
               (const void *)this, name.c_str(), may_clear);

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

Stock::Stock(struct pool &_pool,
             const StockClass &_cls, void *_class_ctx,
             const char *_name, unsigned _limit, unsigned _max_idle,
             StockHandler *_handler)
    :pool(*pool_new_libc(&_pool, "stock")),
     cls(_cls), class_ctx(_class_ctx),
     name(_name),
     limit(_limit), max_idle(_max_idle),
     handler(_handler),
     retry_event(MakeSimpleEventCallback(Stock, RetryWaiting), this),
     empty_event(MakeSimpleEventCallback(Stock, CheckEmpty), this),
     cleanup_event(MakeSimpleEventCallback(Stock, CleanupEventCallback), this),
     clear_event(MakeSimpleEventCallback(Stock, ClearEventCallback), this)
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

    retry_event.Deinit();
    empty_event.Deinit();
    cleanup_event.Deinit();
    clear_event.Deinit();

    ClearIdle();

    pool_unref(&pool);
}

void
Stock::DestroyItem(StockItem &item)
{
    item.Destroy(class_ctx);
}

bool
Stock::GetIdle(StockGetHandler &get_handler)
{
    auto i = idle.begin();
    const auto end = idle.end();
    while (i != end) {
        StockItem &item = *i;
        assert(item.is_idle);

        i = idle.erase(i);

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
                 struct async_operation_ref &async_ref)
{
    ++num_create;

    cls.create(class_ctx, pool, {*this, get_handler},
               info, caller_pool, async_ref);
}

void
Stock::Get(struct pool &caller_pool, void *info,
           StockGetHandler &get_handler,
           struct async_operation_ref &async_ref)
{
    may_clear = false;

    if (GetIdle(get_handler))
        return;

    if (limit > 0 && busy.size() + num_create >= limit) {
        /* item limit reached: wait for an item to return */
        auto w = NewFromPool<Stock::Waiting>(caller_pool, *this,
                                             caller_pool, info,
                                             get_handler, async_ref);
        waiting.push_front(*w);
        return;
    }

    GetCreate(caller_pool, info, get_handler, async_ref);
}

StockItem *
Stock::GetNow(struct pool &caller_pool, void *info, GError **error_r)
{
    struct NowRequest final : public StockGetHandler {
#ifndef NDEBUG
        bool created = false;
#endif
        StockItem *item;
        GError *error;

        /* virtual methods from class StockGetHandler */
        void OnStockItemReady(StockItem &_item) override {
#ifndef NDEBUG
            created = true;
#endif

            item = &_item;
        }

        void OnStockItemError(GError *_error) override {
#ifndef NDEBUG
            created = true;
#endif

            item = nullptr;
            error = _error;
        }
    };

    NowRequest data;
    struct async_operation_ref async_ref;

    /* cannot call this on a limited stock */
    assert(limit == 0);

    Get(caller_pool, info, data, async_ref);
    assert(data.created);

    if (data.item == nullptr)
        g_propagate_error(error_r, data.error);

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
Stock::ItemCreateError(StockItem &item, GError *error)
{
    ItemCreateError(item.handler, error);
    item.Destroy(class_ctx);
}

void
Stock::ItemCreateAborted(StockItem &item)
{
    ItemCreateAborted();
    item.Destroy(class_ctx);
}

void
Stock::ItemCreateError(StockGetHandler &get_handler, GError *error)
{
    assert(error != nullptr);
    assert(num_create > 0);
    --num_create;

    get_handler.OnStockItemError(error);

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
