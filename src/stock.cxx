/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "defer_event.h"
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
    struct defer_event retry_event;

    /**
     * This event is used to move the "empty" check out of the current
     * stack, to invoke the handler method in a safe environment.
     */
    struct defer_event empty_event;

    struct event cleanup_event;
    struct event clear_event;

    typedef boost::intrusive::list<StockItem,
                                   boost::intrusive::constant_time_size<true>> ItemList;

    ItemList idle;

    ItemList busy;

    unsigned num_create;

    struct Waiting
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

        Stock &stock;

        struct async_operation operation;

        struct pool &pool;
        void *const info;

        const StockGetHandler &handler;
        void *const handler_ctx;

        struct async_operation_ref &async_ref;

        Waiting(Stock &_stock, struct pool &_pool, void *_info,
                const StockGetHandler &_handler, void *_handler_ctx,
                struct async_operation_ref &_async_ref)
            :stock(_stock), pool(_pool), info(_info),
             handler(_handler), handler_ctx(_handler_ctx),
             async_ref(_async_ref) {}

        ~Waiting() {
            pool_unref(&pool);
        }

        void Destroy() {
            DeleteFromPool(pool, this);
        }
    };

    typedef boost::intrusive::list<Waiting,
                                   boost::intrusive::constant_time_size<false>> WaitingList;

    WaitingList waiting;

    bool may_clear;

    Stock(struct pool &_pool, const StockClass &cls, void *class_ctx,
          const char *uri, unsigned limit, unsigned max_idle,
          const StockHandler *handler, void *handler_ctx);

    ~Stock();

    void Destroy() {
        DeleteFromPool(pool, this);
    }
};

static void
destroy_item(Stock &stock, StockItem &item);

/*
 * The "empty()" handler method.
 *
 */

/**
 * Check if the stock has become empty, and invoke the handler.
 */
static void
stock_check_empty(Stock &stock)
{
    if (stock_is_empty(stock) && stock.handler != nullptr &&
        stock.handler->empty != nullptr)
        stock.handler->empty(stock, stock.uri, stock.handler_ctx);
}

static void
stock_empty_event_callback(gcc_unused int fd, short event gcc_unused,
                           void *ctx)
{
    Stock &stock = *(Stock *)ctx;

    stock_check_empty(stock);
}

static void
stock_schedule_check_empty(Stock &stock)
{
    if (stock_is_empty(stock) && stock.handler != nullptr &&
        stock.handler->empty != nullptr)
        defer_event_add(&stock.empty_event);
}


/*
 * cleanup
 *
 */

static void
stock_schedule_cleanup(Stock &stock)
{
    static const struct timeval tv = { .tv_sec = 20, .tv_usec = 0 };

    evtimer_add(&stock.cleanup_event, &tv);
}

static void
stock_unschedule_cleanup(Stock &stock)
{
    evtimer_del(&stock.cleanup_event);
}

static void
stock_cleanup_event_callback(int fd gcc_unused, short event gcc_unused,
                             void *ctx)
{
    auto &stock = *(Stock *)ctx;

    assert(stock.idle.size() > stock.max_idle);

    /* destroy one third of the idle items */

    for (unsigned i = (stock.idle.size() - stock.max_idle + 2) / 3; i > 0; --i)
        stock.idle.pop_front_and_dispose([&stock](StockItem *item){
                destroy_item(stock, *item);
            });

    /* schedule next cleanup */

    if (stock.idle.size() > stock.max_idle)
        stock_schedule_cleanup(stock);
    else
        stock_check_empty(stock);
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

static bool
stock_get_idle(Stock &stock,
               const StockGetHandler &handler, void *handler_ctx);

static void
stock_get_create(Stock &stock, struct pool &caller_pool, void *info,
                 const StockGetHandler &handler, void *handler_ctx,
                 struct async_operation_ref &async_ref);

/**
 * Retry the waiting requests.  This is called after the number of
 * busy items was reduced.
 */
static void
stock_retry_waiting(Stock &stock)
{
    if (stock.limit == 0)
        /* no limit configured, no waiters possible */
        return;

    /* first try to serve existing idle items */

    while (stock.idle.size() > 0) {
        const auto i = stock.waiting.begin();
        if (i == stock.waiting.end())
            return;

        auto &waiting = *i;

        waiting.operation.Finished();
        stock.waiting.erase(i);

        if (stock_get_idle(stock, waiting.handler, waiting.handler_ctx))
            waiting.Destroy();
        else
            /* didn't work (probably because borrowing the item has
               failed) - re-add to "waiting" list */
            stock.waiting.push_front(waiting);
    }

    /* if we're below the limit, create a bunch of new items */

    auto wi = stock.waiting.begin();
    const auto end = stock.waiting.end();
    for (unsigned i = stock.limit - stock.busy.size() - stock.num_create;
         stock.busy.size() + stock.num_create < stock.limit && i > 0 && wi != end;
         --i) {
        auto &waiting = *wi;

        waiting.operation.Finished();
        wi = stock.waiting.erase(wi);
        stock_get_create(stock, waiting.pool, waiting.info,
                         waiting.handler, waiting.handler_ctx,
                         waiting.async_ref);
        waiting.Destroy();
    }
}

static void
stock_retry_event_callback(gcc_unused int fd, gcc_unused short event,
                           void *ctx)
{
    Stock &stock = *(Stock *)ctx;

    stock_retry_waiting(stock);
}

static void
stock_schedule_retry_waiting(Stock &stock)
{
    if (stock.limit > 0 && !stock.waiting.empty() &&
        stock.busy.size() - stock.num_create < stock.limit)
        defer_event_add(&stock.retry_event);
}


/*
 * clear after 60 seconds idle
 *
 */

static void
stock_schedule_clear(Stock &stock)
{
    static const struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };

    evtimer_add(&stock.clear_event, &tv);
}

static void
stock_clear_idle(Stock &stock)
{
    daemon_log(5, "stock_clear_idle(%p, '%s') num_idle=%zu num_busy=%zu\n",
               (const void *)&stock, stock.uri,
               stock.idle.size(), stock.busy.size());

    auto i = stock.idle.begin();
    const auto end = stock.idle.end();
    while (i != end) {
        StockItem &item = *i;

        i = stock.idle.erase(i);

        if (stock.idle.size() == stock.max_idle)
            stock_unschedule_cleanup(stock);

        destroy_item(stock, item);
    }

    assert(stock.idle.empty());
}

static void
stock_clear_event_callback(int fd gcc_unused, short event gcc_unused,
                           void *ctx)
{
    Stock &stock = *(Stock *)ctx;

    daemon_log(6, "stock_clear_event_callback(%p, '%s') may_clear=%d\n",
               (const void *)&stock, stock.uri, stock.may_clear);

    if (stock.may_clear)
        stock_clear_idle(stock);

    stock.may_clear = true;
    stock_schedule_clear(stock);
    stock_check_empty(stock);
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
     handler(_handler), handler_ctx(_handler_ctx)
{
    defer_event_init(&retry_event, stock_retry_event_callback, this);
    defer_event_init(&empty_event, stock_empty_event_callback, this);
    evtimer_set(&cleanup_event, stock_cleanup_event_callback, this);
    evtimer_set(&clear_event, stock_clear_event_callback, this);

    num_create = 0;

    may_clear = false;
    stock_schedule_clear(*this);
}

inline Stock::~Stock()
{
    assert(num_create == 0);

    /* must not call stock_free() when there are busy items left */
    assert(busy.empty());

    defer_event_deinit(&retry_event);
    defer_event_deinit(&empty_event);
    evtimer_del(&cleanup_event);
    evtimer_del(&clear_event);

    stock_clear_idle(*this);

    pool_unref(&pool);
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

static void
stock_item_free(Stock &stock, StockItem &item)
{
    assert(pool_contains(item.pool, &item, stock.cls.item_size));

    if (item.pool == &stock.pool)
        p_free(&stock.pool, &item);
    else {
        pool_trash(item.pool);
        pool_unref(item.pool);
    }
}

static void
destroy_item(Stock &stock, StockItem &item)
{
    assert(pool_contains(item.pool, &item, stock.cls.item_size));

    stock.cls.destroy(stock.class_ctx, item);
    stock_item_free(stock, item);
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
    return stock.idle.size() == 0 && stock.busy.empty() &&
        stock.num_create == 0;
}

void
stock_add_stats(const Stock &stock, StockStats &data)
{
    data.busy += stock.busy.size();
    data.idle += stock.idle.size();
}

static bool
stock_get_idle(Stock &stock,
               const StockGetHandler &handler, void *handler_ctx)
{
    auto i = stock.idle.begin();
    const auto end = stock.idle.end();
    while (i != end) {
        StockItem &item = *i;
        assert(item.is_idle);

        i = stock.idle.erase(i);

        if (stock.idle.size() == stock.max_idle)
            stock_unschedule_cleanup(stock);

        if (stock.cls.borrow(stock.class_ctx, item)) {
#ifndef NDEBUG
            item.is_idle = false;
#endif

            stock.busy.push_front(item);

            handler.ready(item, handler_ctx);
            return true;
        }

        destroy_item(stock, item);
    }

    stock_schedule_check_empty(stock);
    return false;
}

static void
stock_get_create(Stock &stock, struct pool &caller_pool, void *info,
                 const StockGetHandler &handler, void *handler_ctx,
                 struct async_operation_ref &async_ref)
{
    struct pool *pool = stock.cls.pool(stock.class_ctx,
                                       stock.pool, stock.uri);

    auto item = (StockItem *)p_malloc(pool, stock.cls.item_size);
    item->stock = &stock;
    item->pool = pool;
#ifndef NDEBUG
    item->is_idle = false;
#endif
    item->handler = &handler;
    item->handler_ctx = handler_ctx;

    ++stock.num_create;

    stock.cls.create(stock.class_ctx, *item, stock.uri, info,
                     caller_pool, async_ref);
}

void
stock_get(Stock &stock, struct pool &caller_pool, void *info,
          const StockGetHandler &handler, void *handler_ctx,
          struct async_operation_ref &async_ref)
{
    stock.may_clear = false;

    if (stock_get_idle(stock, handler, handler_ctx))
        return;

    if (stock.limit > 0 &&
        stock.busy.size() + stock.num_create >= stock.limit) {
        /* item limit reached: wait for an item to return */
        auto waiting = NewFromPool<Stock::Waiting>(caller_pool, stock,
                                                   caller_pool, info,
                                                   handler, handler_ctx,
                                                   async_ref);

        pool_ref(&caller_pool);

        waiting->operation.Init(stock_wait_operation);
        async_ref.Set(waiting->operation);

        stock.waiting.push_front(*waiting);
        return;
    }

    stock_get_create(stock, caller_pool, info,
                     handler, handler_ctx, async_ref);
}

struct now_data {
#ifndef NDEBUG
    bool created;
#endif
    StockItem *item;
    GError *error;
};

static void
stock_now_ready(StockItem &item, void *ctx)
{
    struct now_data *data = (struct now_data *)ctx;

#ifndef NDEBUG
    data->created = true;
#endif

    data->item = &item;
}

static void
stock_now_error(GError *error, void *ctx)
{
    struct now_data *data = (struct now_data *)ctx;

#ifndef NDEBUG
    data->created = true;
#endif

    data->item = nullptr;
    data->error = error;
}

static const StockGetHandler stock_now_handler = {
    .ready = stock_now_ready,
    .error = stock_now_error,
};

StockItem *
stock_get_now(Stock &stock, struct pool &pool, void *info,
              GError **error_r)
{
    struct now_data data;
#ifndef NDEBUG
    data.created = false;
#endif

    struct async_operation_ref async_ref;

    /* cannot call this on a limited stock */
    assert(stock.limit == 0);

    stock_get(stock, pool, info, stock_now_handler, &data, async_ref);
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

    item.handler->ready(item, item.handler_ctx);
}

void
stock_item_failed(StockItem &item, GError *error)
{
    Stock &stock = *item.stock;

    assert(error != nullptr);
    assert(stock.num_create > 0);
    --stock.num_create;

    item.handler->error(error, item.handler_ctx);
    stock_item_free(stock, item);
    stock_schedule_check_empty(stock);

    stock_schedule_retry_waiting(stock);
}

void
stock_item_aborted(StockItem &item)
{
    Stock &stock = *item.stock;

    assert(stock.num_create > 0);
    --stock.num_create;

    stock_item_free(stock, item);
    stock_schedule_check_empty(stock);

    stock_schedule_retry_waiting(stock);
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

    if (destroy) {
        destroy_item(stock, item);
        stock_schedule_check_empty(stock);
    } else {
#ifndef NDEBUG
        item.is_idle = true;
#endif

        if (stock.idle.size() == stock.max_idle)
            stock_schedule_cleanup(stock);

        stock.idle.push_front(item);

        stock.cls.release(stock.class_ctx, item);
    }

    stock_schedule_retry_waiting(stock);
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
        stock_unschedule_cleanup(stock);

    destroy_item(stock, item);
    stock_schedule_check_empty(stock);
}
