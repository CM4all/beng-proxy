#include "stock.h"
#include "async.h"

#include <glib.h>

#include <event.h>
#include <assert.h>

static unsigned num_create, num_fail, num_borrow, num_release, num_destroy;
static bool next_fail;
static bool got_item;
static struct stock_item *last_item;

struct my_stock_item {
    struct stock_item base;

    void *info;
};

/*
 * stock class
 *
 */

static pool_t
my_stock_pool(G_GNUC_UNUSED void *ctx, pool_t parent,
              G_GNUC_UNUSED const char *uri)
{
    return pool_new_linear(parent, "my_stock", 512);
}

static void
my_stock_create(void *ctx __attr_unused, struct stock_item *_item,
                G_GNUC_UNUSED const char *uri, void *info,
                G_GNUC_UNUSED pool_t caller_pool,
                G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
    struct my_stock_item *item = (struct my_stock_item *)_item;

    item->info = info;

    if (next_fail) {
        ++num_fail;
        stock_item_failed(&item->base);
    } else {
        ++num_create;
        stock_item_available(&item->base);
    }
}

static bool
my_stock_borrow(G_GNUC_UNUSED void *ctx,
                G_GNUC_UNUSED struct stock_item *item)
{
    ++num_borrow;
    return true;
}

static void
my_stock_release(G_GNUC_UNUSED void *ctx,
                 G_GNUC_UNUSED struct stock_item *item)
{
    ++num_release;
}

static void
my_stock_destroy(G_GNUC_UNUSED void *ctx,
                 G_GNUC_UNUSED struct stock_item *_item)
{
    ++num_destroy;
}

static const struct stock_class my_stock_class = {
    .item_size = sizeof(struct my_stock_item),
    .pool = my_stock_pool,
    .create = my_stock_create,
    .borrow = my_stock_borrow,
    .release = my_stock_release,
    .destroy = my_stock_destroy,
};

static void
my_stock_callback(G_GNUC_UNUSED void *ctx, struct stock_item *item)
{
    assert(!got_item);

    got_item = true;
    last_item = item;
}

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv)
{
    struct event_base *event_base;
    pool_t pool;
    struct stock *stock;
    struct async_operation_ref async_ref;
    struct stock_item *item, *second, *third;

    event_base = event_init();
    pool = pool_new_libc(NULL, "root");

    stock = stock_new(pool, &my_stock_class, NULL, NULL, 3);

    /* create first item */

    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(got_item);
    assert(last_item != NULL);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 0 && num_release == 0 && num_destroy == 0);
    item = last_item;

    /* release first item */

    stock_put(item, false);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 0 && num_release == 1 && num_destroy == 0);

    /* reuse first item */

    got_item = false;
    last_item = NULL;
    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(got_item);
    assert(last_item == item);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);

    /* create second item */

    got_item = false;
    last_item = NULL;
    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(got_item);
    assert(last_item != NULL);
    assert(last_item != item);
    assert(num_create == 2 && num_fail == 0);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);
    second = last_item;

    /* fail to create third item */

    next_fail = true;
    got_item = false;
    last_item = NULL;
    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(got_item);
    assert(last_item == NULL);
    assert(num_create == 2 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);

    /* create third item */

    next_fail = false;
    got_item = false;
    last_item = NULL;
    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(got_item);
    assert(last_item != NULL);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);
    third = last_item;

    /* fourth item waiting */

    got_item = false;
    last_item = NULL;
    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(!got_item);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);

    /* fifth item waiting */

    stock_get(stock, pool, NULL, my_stock_callback, NULL, &async_ref);
    assert(!got_item);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);

    /* return third item */

    stock_put(third, false);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 0);
    assert(got_item);
    assert(last_item == third);

    /* destroy second item */

    got_item = false;
    last_item = NULL;
    stock_put(second, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 1);
    assert(got_item);
    assert(last_item != NULL);
    second = last_item;

    /* destroy first item */

    stock_put(item, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 2);

    /* destroy second item */

    stock_put(second, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 3);

    /* destroy third item */

    stock_put(third, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 4);

    /* cleanup */

    stock_free(stock);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
