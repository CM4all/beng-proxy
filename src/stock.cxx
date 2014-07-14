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

struct stock {
    struct pool *pool;
    const struct stock_class *cls;
    void *class_ctx;
    const char *uri;

    /**
     * The maximum number of items in this stock.  If any more items
     * are requested, they are put into the #waiting list, which gets
     * checked as soon as stock_put() is called.
     */
    unsigned limit;

    /**
     * The maximum number of permanent idle items.  If there are more
     * than that, a timer will incrementally kill excess items.
     */
    unsigned max_idle;

    const struct stock_handler *handler;
    void *handler_ctx;

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

    unsigned num_idle;
    struct list_head idle;

    unsigned num_busy;
#ifndef NDEBUG
    struct list_head busy;
#endif

    unsigned num_create;

    struct list_head waiting;

    bool may_clear;
};

struct stock_waiting {
    struct list_head siblings;

    struct async_operation operation;

    struct pool *pool;
    void *info;

    const struct stock_get_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
};

static void
destroy_item(struct stock *stock, struct stock_item *item);

/*
 * The "empty()" handler method.
 *
 */

/**
 * Check if the stock has become empty, and invoke the handler.
 */
static void
stock_check_empty(struct stock *stock)
{
    if (stock_is_empty(stock) && stock->handler != nullptr &&
        stock->handler->empty != nullptr)
        stock->handler->empty(stock, stock->uri, stock->handler_ctx);
}

static void
stock_empty_event_callback(gcc_unused int fd, short event gcc_unused,
                           void *ctx)
{
    struct stock *stock = (struct stock *)ctx;

    stock_check_empty(stock);
}

static void
stock_schedule_check_empty(struct stock *stock)
{
    if (stock_is_empty(stock) && stock->handler != nullptr &&
        stock->handler->empty != nullptr)
        defer_event_add(&stock->empty_event);
}


/*
 * cleanup
 *
 */

static void
stock_schedule_cleanup(struct stock *stock)
{
    static const struct timeval tv = { .tv_sec = 20, .tv_usec = 0 };

    evtimer_add(&stock->cleanup_event, &tv);
}

static void
stock_unschedule_cleanup(struct stock *stock)
{
    evtimer_del(&stock->cleanup_event);
}

static void
stock_cleanup_event_callback(int fd gcc_unused, short event gcc_unused,
                             void *ctx)
{
    struct stock *stock = (struct stock *)ctx;

    assert(stock->num_idle > stock->max_idle);

    /* destroy one third of the idle items */

    for (unsigned i = (stock->num_idle + 2) / 3; i > 0; --i) {
        struct stock_item *item = (struct stock_item *)stock->idle.next;

        assert(!list_empty(&stock->idle));

        list_remove(&item->siblings);
        --stock->num_idle;

        destroy_item(stock, item);
    }

    /* schedule next cleanup */

    if (stock->num_idle > stock->max_idle)
        stock_schedule_cleanup(stock);
    else
        stock_check_empty(stock);
}


/*
 * wait operation
 *
 */

static struct stock_waiting *
async_to_waiting(struct async_operation *ao)
{
    return ContainerCast(ao, struct stock_waiting, operation);
}

static void
stock_wait_abort(struct async_operation *ao)
{
    struct stock_waiting *waiting = async_to_waiting(ao);

    list_remove(&waiting->siblings);
    pool_unref(waiting->pool);
}

static const struct async_operation_class stock_wait_operation = {
    .abort = stock_wait_abort,
};

static bool
stock_get_idle(struct stock *stock,
               const struct stock_get_handler *handler, void *handler_ctx);

static void
stock_get_create(struct stock *stock, struct pool *caller_pool, void *info,
                 const struct stock_get_handler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref);

/**
 * Retry the waiting requests.  This is called after the number of
 * busy items was reduced.
 */
static void
stock_retry_waiting(struct stock *stock)
{
    if (stock->limit == 0)
        /* no limit configured, no waiters possible */
        return;

    /* first try to serve existing idle items */

    while (stock->num_idle > 0) {
        struct stock_waiting *waiting =
            (struct stock_waiting *)stock->waiting.next;

        if (list_empty(&stock->waiting))
            return;

        waiting->operation.Finished();
        list_remove(&waiting->siblings);

        if (stock_get_idle(stock, waiting->handler, waiting->handler_ctx))
            pool_unref(waiting->pool);
        else
            /* didn't work (probably because borrowing the item has
               failed) - re-add to "waiting" list */
            list_add(&waiting->siblings, &stock->waiting);
    }

    /* if we're below the limit, create a bunch of new items */

    for (unsigned i = stock->limit - stock->num_busy - stock->num_create;
         stock->num_busy + stock->num_create < stock->limit && i > 0; --i) {
        struct stock_waiting *waiting =
            (struct stock_waiting *)stock->waiting.next;

        if (list_empty(&stock->waiting))
            return;

        waiting->operation.Finished();
        list_remove(&waiting->siblings);
        stock_get_create(stock, waiting->pool, waiting->info,
                         waiting->handler, waiting->handler_ctx,
                         waiting->async_ref);
        pool_unref(waiting->pool);
    }
}

static void
stock_retry_event_callback(gcc_unused int fd, gcc_unused short event,
                           void *ctx)
{
    struct stock *stock = (struct stock *)ctx;

    stock_retry_waiting(stock);
}

static void
stock_schedule_retry_waiting(struct stock *stock)
{
    if (stock->limit > 0 && !list_empty(&stock->waiting) &&
        stock->num_busy - stock->num_create < stock->limit)
        defer_event_add(&stock->retry_event);
}


/*
 * clear after 60 seconds idle
 *
 */

static void
stock_schedule_clear(struct stock *stock)
{
    static const struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };

    evtimer_add(&stock->clear_event, &tv);
}

static void
stock_clear_idle(struct stock *stock)
{
    daemon_log(5, "stock_clear_idle(%p, '%s') num_idle=%u num_busy=%u\n",
               (const void *)stock, stock->uri,
               stock->num_idle, stock->num_busy);

    while (stock->num_idle > 0) {
        struct stock_item *item = (struct stock_item *)stock->idle.next;

        assert(!list_empty(&stock->idle));

        list_remove(&item->siblings);
        --stock->num_idle;

        if (stock->num_idle == stock->max_idle)
            stock_unschedule_cleanup(stock);

        destroy_item(stock, item);
    }

    assert(list_empty(&stock->idle));
}

static void
stock_clear_event_callback(int fd gcc_unused, short event gcc_unused,
                           void *ctx)
{
    struct stock *stock = (struct stock *)ctx;

    daemon_log(6, "stock_clear_event_callback(%p, '%s') may_clear=%d\n",
               (const void *)stock, stock->uri, stock->may_clear);

    if (stock->may_clear)
        stock_clear_idle(stock);

    stock->may_clear = true;
    stock_schedule_clear(stock);
    stock_check_empty(stock);
}


/*
 * constructor
 *
 */

struct stock *
stock_new(struct pool *pool, const struct stock_class *cls,
          void *class_ctx, const char *uri, unsigned limit, unsigned max_idle,
          const struct stock_handler *handler, void *handler_ctx)
{
    assert(pool != nullptr);
    assert(cls != nullptr);
    assert(cls->item_size > sizeof(struct stock_item));
    assert(cls->pool != nullptr);
    assert(cls->create != nullptr);
    assert(cls->borrow != nullptr);
    assert(cls->release != nullptr);
    assert(cls->destroy != nullptr);
    assert(max_idle > 0);

    pool = pool_new_linear(pool, "stock", 1024);

    auto stock = NewFromPool<struct stock>(*pool);
    stock->pool = pool;
    stock->cls = cls;
    stock->class_ctx = class_ctx;
    stock->uri = uri == nullptr ? nullptr : p_strdup(pool, uri);
    stock->limit = limit;
    stock->max_idle = max_idle;
    stock->handler = handler;
    stock->handler_ctx = handler_ctx;

    defer_event_init(&stock->retry_event, stock_retry_event_callback, stock);
    defer_event_init(&stock->empty_event, stock_empty_event_callback, stock);
    evtimer_set(&stock->cleanup_event, stock_cleanup_event_callback, stock);
    evtimer_set(&stock->clear_event, stock_clear_event_callback, stock);

    stock->num_idle = 0;
    list_init(&stock->idle);

    stock->num_busy = 0;
#ifndef NDEBUG
    list_init(&stock->busy);
#endif

    stock->num_create = 0;

    if (limit > 0)
        list_init(&stock->waiting);

    stock->may_clear = false;
    stock_schedule_clear(stock);

    return stock;
}


static void
stock_item_free(struct stock *stock, struct stock_item *item)
{
    assert(pool_contains(item->pool, item, stock->cls->item_size));

    if (item->pool == stock->pool)
        p_free(stock->pool, item);
    else {
        pool_trash(item->pool);
        pool_unref(item->pool);
    }
}

static void
destroy_item(struct stock *stock, struct stock_item *item)
{
    assert(pool_contains(item->pool, item, stock->cls->item_size));

    stock->cls->destroy(stock->class_ctx, item);
    stock_item_free(stock, item);
}

void
stock_free(struct stock *stock)
{
    assert(stock != nullptr);
    assert(stock->num_busy == 0);
    assert(stock->num_create == 0);

    /* must not call stock_free() when there are busy items left */
    assert(list_empty(&stock->busy));

    defer_event_deinit(&stock->retry_event);
    defer_event_deinit(&stock->empty_event);
    evtimer_del(&stock->cleanup_event);
    evtimer_del(&stock->clear_event);

    stock_clear_idle(stock);

    pool_unref(stock->pool);
}

const char *
stock_get_uri(struct stock *stock)
{
    assert(stock != nullptr);

    return stock->uri;
}

bool
stock_is_empty(const struct stock *stock)
{
    return stock->num_idle == 0 && stock->num_busy == 0 &&
        stock->num_create == 0;
}

void
stock_add_stats(const struct stock *stock, struct stock_stats *data)
{
    data->busy += stock->num_busy;
    data->idle += stock->num_idle;
}

static bool
stock_get_idle(struct stock *stock,
               const struct stock_get_handler *handler, void *handler_ctx)
{
    while (stock->num_idle > 0) {
        assert(!list_empty(&stock->idle));

        struct stock_item *item = (struct stock_item *)stock->idle.next;
        assert(item->is_idle);

        list_remove(&item->siblings);
        --stock->num_idle;

        if (stock->num_idle == stock->max_idle)
            stock_unschedule_cleanup(stock);

        if (stock->cls->borrow(stock->class_ctx, item)) {
#ifndef NDEBUG
            item->is_idle = false;
            list_add(&item->siblings, &stock->busy);
#endif
            ++stock->num_busy;

            handler->ready(item, handler_ctx);
            return true;
        }

        destroy_item(stock, item);
    }

    stock_schedule_check_empty(stock);
    return false;
}

static void
stock_get_create(struct stock *stock, struct pool *caller_pool, void *info,
                 const struct stock_get_handler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    struct pool *pool;

    pool = stock->cls->pool(stock->class_ctx, stock->pool, stock->uri);

    auto item = (struct stock_item *)p_malloc(pool, stock->cls->item_size);
    item->stock = stock;
    item->pool = pool;
#ifndef NDEBUG
    item->is_idle = false;
#endif
    item->handler = handler;
    item->handler_ctx = handler_ctx;

    ++stock->num_create;

    stock->cls->create(stock->class_ctx, item, stock->uri, info,
                         caller_pool, async_ref);
}

void
stock_get(struct stock *stock, struct pool *caller_pool, void *info,
          const struct stock_get_handler *handler, void *handler_ctx,
          struct async_operation_ref *async_ref)
{
    assert(stock != nullptr);

    stock->may_clear = false;

    if (stock_get_idle(stock, handler, handler_ctx))
        return;

    if (stock->limit > 0 &&
        stock->num_busy + stock->num_create >= stock->limit) {
        /* item limit reached: wait for an item to return */
        auto waiting = NewFromPool<struct stock_waiting>(*caller_pool);

        pool_ref(caller_pool);
        waiting->pool = caller_pool;
        waiting->info = info;
        waiting->handler = handler;
        waiting->handler_ctx = handler_ctx;
        waiting->async_ref = async_ref;

        waiting->operation.Init(stock_wait_operation);
        async_ref->Set(waiting->operation);

        list_add(&waiting->siblings, &stock->waiting);
        return;
    }

    stock_get_create(stock, caller_pool, info,
                     handler, handler_ctx, async_ref);
}

struct now_data {
#ifndef NDEBUG
    bool created;
#endif
    struct stock_item *item;
    GError *error;
};

static void
stock_now_ready(struct stock_item *item, void *ctx)
{
    struct now_data *data = (struct now_data *)ctx;

#ifndef NDEBUG
    data->created = true;
#endif

    data->item = item;
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

static const struct stock_get_handler stock_now_handler = {
    .ready = stock_now_ready,
    .error = stock_now_error,
};

struct stock_item *
stock_get_now(struct stock *stock, struct pool *pool, void *info,
              GError **error_r)
{
    struct now_data data;
#ifndef NDEBUG
    data.created = false;
#endif

    struct async_operation_ref async_ref;

    /* cannot call this on a limited stock */
    assert(stock->limit == 0);

    stock_get(stock, pool, info, &stock_now_handler, &data, &async_ref);
    assert(data.created);

    if (data.item == nullptr)
        g_propagate_error(error_r, data.error);

    return data.item;
}

void
stock_item_available(struct stock_item *item)
{
    struct stock *stock = item->stock;

    assert(stock->num_create > 0);
    --stock->num_create;

#ifndef NDEBUG
    list_add(&item->siblings, &stock->busy);
#endif
    ++stock->num_busy;

    item->handler->ready(item, item->handler_ctx);
}

void
stock_item_failed(struct stock_item *item, GError *error)
{
    struct stock *stock = item->stock;

    assert(error != nullptr);
    assert(stock->num_create > 0);
    --stock->num_create;

    item->handler->error(error, item->handler_ctx);
    stock_item_free(stock, item);
    stock_schedule_check_empty(stock);

    stock_schedule_retry_waiting(stock);
}

void
stock_item_aborted(struct stock_item *item)
{
    struct stock *stock = item->stock;

    assert(stock->num_create > 0);
    --stock->num_create;

    stock_item_free(stock, item);
    stock_schedule_check_empty(stock);

    stock_schedule_retry_waiting(stock);
}

void
stock_put(struct stock_item *item, bool destroy)
{
    assert(item != nullptr);
    assert(!item->is_idle);

    struct stock *stock = item->stock;
    stock->may_clear = false;

    assert(stock->num_busy > 0);

    assert(stock != nullptr);
    assert(pool_contains(item->pool, item, stock->cls->item_size));

#ifndef NDEBUG
    list_remove(&item->siblings);
#endif
    --stock->num_busy;

    if (destroy) {
        destroy_item(stock, item);
        stock_schedule_check_empty(stock);
    } else {
#ifndef NDEBUG
        item->is_idle = true;
#endif

        if (stock->num_idle == stock->max_idle)
            stock_schedule_cleanup(stock);

        list_add(&item->siblings, &stock->idle);
        ++stock->num_idle;

        stock->cls->release(stock->class_ctx, item);
    }

    stock_schedule_retry_waiting(stock);
}

void
stock_del(struct stock_item *item)
{
    assert(item != nullptr);
    assert(item->is_idle);

    struct stock *stock = item->stock;

    assert(stock != nullptr);
    assert(stock->num_idle > 0);
    assert(!list_empty(&stock->idle));
    assert(pool_contains(item->pool, item, stock->cls->item_size));
    assert(item->siblings.next->prev == &item->siblings);
    assert(item->siblings.prev->next == &item->siblings);

    list_remove(&item->siblings);
    --stock->num_idle;

    if (stock->num_idle == stock->max_idle)
        stock_unschedule_cleanup(stock);

    destroy_item(stock, item);
    stock_schedule_check_empty(stock);
}
