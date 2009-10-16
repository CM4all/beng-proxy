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

    struct event cleanup_event;
    struct event clear_event;

    unsigned num_idle;
    struct list_head idle;

    unsigned num_busy;
#ifndef NDEBUG
    struct list_head busy;
#endif

    bool may_clear;
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
          void *class_ctx, const char *uri)
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

    evtimer_set(&stock->cleanup_event, stock_cleanup_event_callback, stock);
    evtimer_set(&stock->clear_event, stock_clear_event_callback, stock);

    stock->num_idle = 0;
    list_init(&stock->idle);

    stock->num_busy = 0;
#ifndef NDEBUG
    list_init(&stock->busy);
#endif

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
    return stock->num_idle == 0 && stock->num_busy == 0;
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

    stock_get(stock, pool, info, stock_now_callback, &data, &async_ref);
    assert(data.created);

    return data.item;
}

void
stock_item_available(struct stock_item *item)
{
    struct stock *stock = item->stock;

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

    item->callback(item->callback_ctx, NULL);
    stock_item_free(stock, item);
}

void
stock_item_aborted(struct stock_item *item)
{
    stock_item_free(item->stock, item);
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
