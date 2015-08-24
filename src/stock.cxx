/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "event/Event.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>

struct Stock {
    struct pool &pool;
    const StockClass &cls;
    void *const class_ctx;
    const char *const uri;

    /**
     * The maximum number of items in this stock.  If any more items
     * are requested, they are put into the #waiting list, which gets
     * checked as soon as stock_put() is called.
     */
    const unsigned limit;

    /**
     * The maximum number of permanent idle items.  If there are more
     * than that, a timer will incrementally kill excess items.
     */
    const unsigned max_idle;

    const StockHandler *const handler;
    void *const handler_ctx;

    /**
     * This event is used to move the "retry waiting" code out of the
     * current stack, to invoke the handler method in a safe
     * environment.
     */
    DeferEvent retry_event;

    /**
     * This event is used to move the "empty" check out of the current
     * stack, to invoke the handler method in a safe environment.
     */
    DeferEvent empty_event;

    Event cleanup_event;
    Event clear_event;

    typedef boost::intrusive::list<StockItem,
                                   boost::intrusive::constant_time_size<true>> ItemList;

    ItemList idle;

    ItemList busy;

    unsigned num_create = 0;

    struct Waiting
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

        Stock &stock;

        struct async_operation operation;

        struct pool &pool;
        void *const info;

        StockGetHandler &handler;

        struct async_operation_ref &async_ref;

        Waiting(Stock &_stock, struct pool &_pool, void *_info,
                StockGetHandler &_handler,
                struct async_operation_ref &_async_ref)
            :stock(_stock), pool(_pool), info(_info),
             handler(_handler),
             async_ref(_async_ref) {}

        void Destroy() {
            DeleteUnrefPool(pool, this);
        }
    };

    typedef boost::intrusive::list<Waiting,
                                   boost::intrusive::constant_time_size<false>> WaitingList;

    WaitingList waiting;

    bool may_clear = false;

    Stock(struct pool &_pool, const StockClass &cls, void *class_ctx,
          const char *uri, unsigned limit, unsigned max_idle,
          const StockHandler *handler, void *handler_ctx);

    ~Stock();

    void Destroy() {
        DeleteUnrefPool(pool, this);
    }

    gcc_pure
    bool IsEmpty() const {
        return idle.empty() && busy.empty() && num_create == 0;
    }

    /**
     * Check if the stock has become empty, and invoke the handler.
     */
    void CheckEmpty();
    void ScheduleCheckEmpty();

    void FreeItem(StockItem &item);
    void DestroyItem(StockItem &item);

    void ScheduleClear() {
        static constexpr struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };
        clear_event.Add(&tv);
    }

    void ClearIdle();

    bool GetIdle(StockGetHandler &handler);
    void GetCreate(struct pool &caller_pool, void *info,
                   StockGetHandler &get_handler,
                   struct async_operation_ref &async_ref);

    /**
     * Retry the waiting requests.  This is called after the number of
     * busy items was reduced.
     */
    void RetryWaiting();
    void ScheduleRetryWaiting();

    void ScheduleCleanup() {
        static constexpr struct timeval tv = { .tv_sec = 20, .tv_usec = 0 };
        cleanup_event.Add(&tv);
    }

    void UnscheduleCleanup() {
        cleanup_event.Delete();
    }
    void CleanupEventCallback();
    void ClearEventCallback();
};

/*
 * The "empty()" handler method.
 *
 */

void
Stock::CheckEmpty()
{
    if (IsEmpty() && handler != nullptr && handler->empty != nullptr)
        handler->empty(*this, uri, handler_ctx);
}

void
Stock::ScheduleCheckEmpty()
{
    if (IsEmpty() && handler != nullptr && handler->empty != nullptr)
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

static Stock::Waiting *
async_to_waiting(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &Stock::Waiting::operation);
}

static void
stock_wait_abort(struct async_operation *ao)
{
    auto *waiting = async_to_waiting(ao);

    auto &list = waiting->stock.waiting;
    const auto i = list.iterator_to(*waiting);
    list.erase_and_dispose(i, [](Stock::Waiting *w){ w->Destroy(); });
}

static const struct async_operation_class stock_wait_operation = {
    .abort = stock_wait_abort,
};

void
Stock::RetryWaiting()
{
    if (limit == 0)
        /* no limit configured, no waiters possible */
        return;

    /* first try to serve existing idle items */

    while (idle.size() > 0) {
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

    auto wi = waiting.begin();
    const auto end = waiting.end();
    for (unsigned i = limit - busy.size() - num_create;
         busy.size() + num_create < limit && i > 0 && wi != end;
         --i) {
        auto &w = *wi;

        w.operation.Finished();
        wi = waiting.erase(wi);
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
               (const void *)this, uri,
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
    daemon_log(6, "stock_clear_event_callback(%p, '%s') may_clear=%d\n",
               (const void *)this, uri, may_clear);

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

inline Stock::Stock(struct pool &_pool,
                    const StockClass &_cls, void *_class_ctx,
                    const char *_uri, unsigned _limit, unsigned _max_idle,
                    const StockHandler *_handler, void *_handler_ctx)
    :pool(_pool), cls(_cls), class_ctx(_class_ctx),
     uri(p_strdup_checked(&pool, _uri)),
     limit(_limit), max_idle(_max_idle),
     handler(_handler), handler_ctx(_handler_ctx),
     retry_event(MakeSimpleEventCallback(Stock, RetryWaiting), this),
     empty_event(MakeSimpleEventCallback(Stock, CheckEmpty), this)
{
    cleanup_event.SetTimer(MakeSimpleEventCallback(Stock,
                                                   CleanupEventCallback),
                           this);
    clear_event.SetTimer(MakeSimpleEventCallback(Stock, ClearEventCallback),
                         this);

    ScheduleClear();
}

inline Stock::~Stock()
{
    assert(num_create == 0);

    /* must not call stock_free() when there are busy items left */
    assert(busy.empty());

    retry_event.Deinit();
    empty_event.Deinit();
    cleanup_event.Delete();
    clear_event.Delete();

    ClearIdle();
}

Stock *
stock_new(struct pool &_pool, const StockClass &cls, void *class_ctx,
          const char *uri, unsigned limit, unsigned max_idle,
          const StockHandler *handler, void *handler_ctx)
{
    assert(cls.item_size > sizeof(StockItem));
    assert(cls.pool != nullptr);
    assert(cls.create != nullptr);
    assert(cls.borrow != nullptr);
    assert(cls.release != nullptr);
    assert(cls.destroy != nullptr);
    assert(max_idle > 0);

    struct pool *pool = pool_new_linear(&_pool, "stock", 1024);

    return NewFromPool<Stock>(*pool, *pool, cls, class_ctx,
                              uri, limit, max_idle,
                              handler, handler_ctx);
}

void
Stock::FreeItem(StockItem &item)
{
    assert(pool_contains(item.pool, &item, cls.item_size));

    if (item.pool == &pool)
        p_free(&pool, &item);
    else {
        pool_trash(item.pool);
        pool_unref(item.pool);
    }
}

void
Stock::DestroyItem(StockItem &item)
{
    assert(pool_contains(item.pool, &item, cls.item_size));

    cls.destroy(class_ctx, item);
    FreeItem(item);
}

void
stock_free(Stock *stock)
{
    assert(stock != nullptr);

    stock->Destroy();
}

const char *
stock_get_uri(Stock &stock)
{
    return stock.uri;
}

bool
stock_is_empty(const Stock &stock)
{
    return stock.IsEmpty();
}

void
stock_add_stats(const Stock &stock, StockStats &data)
{
    data.busy += stock.busy.size();
    data.idle += stock.idle.size();
}

void
stock_fade_all(Stock &stock)
{
    for (auto &i : stock.busy)
        i.fade = true;

    stock.ClearIdle();
    stock.ScheduleCheckEmpty();

    // TODO: restart the "num_create" list?
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

        if (cls.borrow(class_ctx, item)) {
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
    struct pool *item_pool = cls.pool(class_ctx, pool, uri);

    auto item = (StockItem *)p_malloc(item_pool, cls.item_size);
    item->stock = this;
    item->pool = item_pool;
    item->handler = &get_handler;

    item->fade = false;

#ifndef NDEBUG
    item->is_idle = false;
#endif

    ++num_create;

    cls.create(class_ctx, *item, uri, info, caller_pool, async_ref);
}

void
stock_get(Stock &stock, struct pool &caller_pool, void *info,
          StockGetHandler &handler,
          struct async_operation_ref &async_ref)
{
    stock.may_clear = false;

    if (stock.GetIdle(handler))
        return;

    if (stock.limit > 0 &&
        stock.busy.size() + stock.num_create >= stock.limit) {
        /* item limit reached: wait for an item to return */
        auto waiting = NewFromPool<Stock::Waiting>(caller_pool, stock,
                                                   caller_pool, info,
                                                   handler, async_ref);

        pool_ref(&caller_pool);

        waiting->operation.Init(stock_wait_operation);
        async_ref.Set(waiting->operation);

        stock.waiting.push_front(*waiting);
        return;
    }

    stock.GetCreate(caller_pool, info, handler, async_ref);
}

StockItem *
stock_get_now(Stock &stock, struct pool &pool, void *info,
              GError **error_r)
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
    assert(stock.limit == 0);

    stock_get(stock, pool, info, data, async_ref);
    assert(data.created);

    if (data.item == nullptr)
        g_propagate_error(error_r, data.error);

    return data.item;
}

void
stock_item_available(StockItem &item)
{
    Stock &stock = *item.stock;

    assert(stock.num_create > 0);
    --stock.num_create;

    stock.busy.push_front(item);

    item.handler->OnStockItemReady(item);
}

void
stock_item_failed(StockItem &item, GError *error)
{
    Stock &stock = *item.stock;

    assert(error != nullptr);
    assert(stock.num_create > 0);
    --stock.num_create;

    item.handler->OnStockItemError(error);
    stock.FreeItem(item);
    stock.ScheduleCheckEmpty();

    stock.ScheduleRetryWaiting();
}

void
stock_item_aborted(StockItem &item)
{
    Stock &stock = *item.stock;

    assert(stock.num_create > 0);
    --stock.num_create;

    stock.FreeItem(item);
    stock.ScheduleCheckEmpty();

    stock.ScheduleRetryWaiting();
}

void
stock_put(StockItem &item, bool destroy)
{
    assert(!item.is_idle);

    Stock &stock = *item.stock;
    stock.may_clear = false;

    assert(!stock.busy.empty());

    assert(pool_contains(item.pool, &item, stock.cls.item_size));

    stock.busy.erase(stock.busy.iterator_to(item));

    if (destroy || item.fade) {
        stock.DestroyItem(item);
        stock.ScheduleCheckEmpty();
    } else {
#ifndef NDEBUG
        item.is_idle = true;
#endif

        if (stock.idle.size() == stock.max_idle)
            stock.ScheduleCleanup();

        stock.idle.push_front(item);

        stock.cls.release(stock.class_ctx, item);
    }

    stock.ScheduleRetryWaiting();
}

void
stock_del(StockItem &item)
{
    assert(item.is_idle);

    Stock &stock = *item.stock;

    assert(!stock.idle.empty());
    assert(pool_contains(item.pool, &item, stock.cls.item_size));

    stock.idle.erase(stock.idle.iterator_to(item));

    if (stock.idle.size() == stock.max_idle)
        stock.UnscheduleCleanup();

    stock.DestroyItem(item);
    stock.ScheduleCheckEmpty();
}
