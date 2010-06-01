/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.h"
#include "async.h"

#include <daemon/log.h>

#include <event.h>

#include <assert.h>

enum {
    MAX_IDLE = 8,
};

struct stock {
    pool_t pool;
    const struct stock_class *class;
    void *class_ctx;
    const char *uri;

    /**
     * The maximum number of items in this stock.  If any more items
     * are requested, they are put into the #waiting list, which gets
     * checked as soon as stock_put() is called.
     */
    unsigned limit;

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

    pool_t pool;
    void *info;
    stock_callback_t callback;
    void *callback_ctx;
    struct async_operation_ref *async_ref;
};

static void
destroy_item(struct stock *stock, struct stock_item *item);

/*
 * cleanup
 *
 */

static void
stock_schedule_cleanup(struct stock *stock)
{
    static const struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

    evtimer_add(&stock->cleanup_event, &tv);
}

static void
stock_unschedule_cleanup(struct stock *stock)
{
    evtimer_del(&stock->cleanup_event);
}

static void
stock_cleanup_event_callback(int fd __attr_unused, short event __attr_unused,
                             void *ctx)
{
    struct stock *stock = ctx;

    /* destroy half of the idle items */

    for (unsigned i = (stock->num_idle + 1) / 2; i > 0; --i) {
        struct stock_item *item = (struct stock_item *)stock->idle.next;

        assert(!list_empty(&stock->idle));

        list_remove(&item->list_head);
        --stock->num_idle;

        destroy_item(stock, item);
    }

    /* schedule next cleanup */

    if (stock->num_idle > MAX_IDLE)
        stock_schedule_cleanup(stock);
}


/*
 * wait operation
 *
 */

static struct stock_waiting *
async_to_waiting(struct async_operation *ao)
{
    return (struct stock_waiting*)(((char*)ao) - offsetof(struct stock_waiting, operation));
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
               stock_callback_t callback, void *callback_ctx);

static void
stock_get_create(struct stock *stock, pool_t caller_pool, void *info,
                 stock_callback_t callback, void *callback_ctx,
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

        async_operation_finished(&waiting->operation);
        list_remove(&waiting->siblings);

        if (stock_get_idle(stock, waiting->callback, waiting->callback_ctx))
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

        async_operation_finished(&waiting->operation);
        list_remove(&waiting->siblings);
        stock_get_create(stock, waiting->pool, waiting->info,
                         waiting->callback, waiting->callback_ctx,
                         waiting->async_ref);
        pool_unref(waiting->pool);
    }
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

        list_remove(&item->list_head);
        --stock->num_idle;

        destroy_item(stock, item);
    }

    assert(list_empty(&stock->idle));
}

static void
stock_clear_event_callback(int fd __attr_unused, short event __attr_unused,
                           void *ctx)
{
    struct stock *stock = ctx;

    daemon_log(6, "stock_clear_event_callback(%p, '%s') may_clear=%d\n",
               (const void *)stock, stock->uri, stock->may_clear);

    if (stock->may_clear)
        stock_clear_idle(stock);

    stock->may_clear = true;
    stock_schedule_clear(stock);
}


/*
 * constructor
 *
 */

struct stock *
stock_new(pool_t pool, const struct stock_class *class,
          void *class_ctx, const char *uri, unsigned limit)
{
    struct stock *stock;

    assert(pool != NULL);
    assert(class != NULL);
    assert(class->item_size > sizeof(struct stock_item));
    assert(class->pool != NULL);
    assert(class->create != NULL);
    assert(class->borrow != NULL);
    assert(class->release != NULL);
    assert(class->destroy != NULL);

    pool = pool_new_linear(pool, "stock", 1024);
    stock = p_malloc(pool, sizeof(*stock));
    stock->pool = pool;
    stock->class = class;
    stock->class_ctx = class_ctx;
    stock->uri = uri == NULL ? NULL : p_strdup(pool, uri);
    stock->limit = limit;

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

    return stock;
}


static void
stock_item_free(struct stock *stock, struct stock_item *item)
{
    assert(pool_contains(item->pool, item, stock->class->item_size));

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
    assert(pool_contains(item->pool, item, stock->class->item_size));

    stock->class->destroy(stock->class_ctx, item);
    stock_item_free(stock, item);
}

void
stock_free(struct stock *stock)
{
    assert(stock != NULL);
    assert(stock->num_busy == 0);
    assert(stock->num_create == 0);

    /* must not call stock_free() when there are busy items left */
    assert(list_empty(&stock->busy));

    evtimer_del(&stock->cleanup_event);
    evtimer_del(&stock->clear_event);

    stock_clear_idle(stock);

    pool_unref(stock->pool);
}

bool
stock_is_empty(const struct stock *stock)
{
    return stock->num_idle == 0 && stock->num_busy == 0 &&
        stock->num_create == 0;
}

static bool
stock_get_idle(struct stock *stock,
               stock_callback_t callback, void *callback_ctx)
{
    while (stock->num_idle > 0) {
        struct stock_item *item = (struct stock_item *)stock->idle.next;

        assert(!list_empty(&stock->idle));

        list_remove(&item->list_head);
        --stock->num_idle;

        if (stock->num_idle == MAX_IDLE)
            stock_unschedule_cleanup(stock);

        assert(item->is_idle);

        if (stock->class->borrow(stock->class_ctx, item)) {
#ifndef NDEBUG
            item->is_idle = false;
            list_add(&item->list_head, &stock->busy);
#endif
            ++stock->num_busy;

            callback(callback_ctx, item);
            return true;
        }

        destroy_item(stock, item);
    }

    return false;
}

static void
stock_get_create(struct stock *stock, pool_t caller_pool, void *info,
                 stock_callback_t callback, void *callback_ctx,
                 struct async_operation_ref *async_ref)
{
    pool_t pool;
    struct stock_item *item;

    pool = stock->class->pool(stock->class_ctx, stock->pool, stock->uri);

    item = p_malloc(pool, stock->class->item_size);
    item->stock = stock;
    item->pool = pool;
#ifndef NDEBUG
    item->is_idle = false;
#endif
    item->callback = callback;
    item->callback_ctx = callback_ctx;

    ++stock->num_create;

    stock->class->create(stock->class_ctx, item, stock->uri, info,
                         caller_pool, async_ref);
}

void
stock_get(struct stock *stock, pool_t caller_pool, void *info,
          stock_callback_t callback, void *callback_ctx,
          struct async_operation_ref *async_ref)
{
    assert(stock != NULL);

    stock->may_clear = false;

    if (stock_get_idle(stock, callback, callback_ctx))
        return;

    if (stock->limit > 0 &&
        stock->num_busy + stock->num_create >= stock->limit) {
        /* item limit reached: wait for an item to return */
        struct stock_waiting *waiting =
            p_malloc(caller_pool, sizeof(*waiting));

        pool_ref(caller_pool);
        waiting->pool = caller_pool;
        waiting->info = info;
        waiting->callback = callback;
        waiting->callback_ctx = callback_ctx;
        waiting->async_ref = async_ref;

        async_init(&waiting->operation, &stock_wait_operation);
        async_ref_set(async_ref, &waiting->operation);

        list_add(&waiting->siblings, &stock->waiting);
        return;
    }

    stock_get_create(stock, caller_pool, info,
                     callback, callback_ctx, async_ref);
}

struct now_data {
#ifndef NDEBUG
    bool created;
#endif
    struct stock_item *item;
};

static void
stock_now_callback(void *ctx, struct stock_item *item)
{
    struct now_data *data = ctx;

#ifndef NDEBUG
    data->created = true;
#endif

    data->item = item;
}

struct stock_item *
stock_get_now(struct stock *stock, pool_t pool, void *info)
{
    struct now_data data = {
#ifndef NDEBUG
        .created = false
#endif
    };
    struct async_operation_ref async_ref;

    /* cannot call this on a limited stock */
    assert(stock->limit == 0);

    stock_get(stock, pool, info, stock_now_callback, &data, &async_ref);
    assert(data.created);

    return data.item;
}

void
stock_item_available(struct stock_item *item)
{
    struct stock *stock = item->stock;

    assert(stock->num_create > 0);
    --stock->num_create;

#ifndef NDEBUG
    list_add(&item->list_head, &stock->busy);
#endif
    ++stock->num_busy;

    item->callback(item->callback_ctx, item);
}

void
stock_item_failed(struct stock_item *item)
{
    struct stock *stock = item->stock;

    assert(stock->num_create > 0);
    --stock->num_create;

    item->callback(item->callback_ctx, NULL);
    stock_item_free(stock, item);

    stock_retry_waiting(stock);
}

void
stock_item_aborted(struct stock_item *item)
{
    struct stock *stock = item->stock;

    assert(stock->num_create > 0);
    --stock->num_create;

    stock_item_free(stock, item);

    stock_retry_waiting(stock);
}

void
stock_put(struct stock_item *item, bool destroy)
{
    struct stock *stock;

    assert(item != NULL);
    assert(!item->is_idle);

    stock = item->stock;
    stock->may_clear = false;

    assert(stock->num_busy > 0);

    assert(stock != NULL);
    assert(pool_contains(item->pool, item, stock->class->item_size));

#ifndef NDEBUG
    list_remove(&item->list_head);
#endif
    --stock->num_busy;

    if (destroy) {
        destroy_item(stock, item);
    } else {
#ifndef NDEBUG
        item->is_idle = true;
#endif

        if (stock->num_idle == MAX_IDLE)
            stock_schedule_cleanup(stock);

        list_add(&item->list_head, &stock->idle);
        ++stock->num_idle;

        stock->class->release(stock->class_ctx, item);
    }

    stock_retry_waiting(stock);
}

void
stock_del(struct stock_item *item)
{
    struct stock *stock;

    assert(item != NULL);
    assert(item->is_idle);

    stock = item->stock;

    assert(stock != NULL);
    assert(stock->num_idle > 0);
    assert(!list_empty(&stock->idle));
    assert(pool_contains(item->pool, item, stock->class->item_size));
    assert(item->list_head.next->prev == &item->list_head);
    assert(item->list_head.prev->next == &item->list_head);

    if (stock->num_idle == MAX_IDLE)
        stock_unschedule_cleanup(stock);

    list_remove(&item->list_head);
    --stock->num_idle;

    destroy_item(stock, item);
}
