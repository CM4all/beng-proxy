/*
 * The 'hstock' class is a hash table of any number of 'stock'
 * objects, each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hstock.hxx"
#include "stock.hxx"
#include "hashmap.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>

struct hstock {
    struct pool *pool;
    const StockClass *cls;
    void *class_ctx;

    /**
     * The maximum number of items in each stock.
     */
    unsigned limit;

    /**
     * The maximum number of permanent idle items in each stock.
     */
    unsigned max_idle;

    struct hashmap *stocks;
};

/*
 * stock handler
 *
 */

static void
hstock_stock_empty(Stock &stock, const char *uri, void *ctx)
{
    struct hstock *hstock = (struct hstock *)ctx;

    daemon_log(5, "hstock(%p) remove empty stock(%p, '%s')\n",
               (const void *)hstock, (const void *)&stock, uri);
    hashmap_remove_existing(hstock->stocks, uri, &stock);

    stock_free(&stock);
}

static constexpr StockHandler hstock_stock_handler = {
    .empty = hstock_stock_empty,
};

struct hstock *
hstock_new(struct pool *pool, const StockClass *cls, void *class_ctx,
           unsigned limit, unsigned max_idle)
{
    assert(pool != nullptr);
    assert(cls != nullptr);
    assert(cls->item_size > sizeof(StockItem));
    assert(cls->create != nullptr);
    assert(cls->borrow != nullptr);
    assert(cls->release != nullptr);
    assert(cls->destroy != nullptr);
    assert(max_idle > 0);

    pool = pool_new_linear(pool, "hstock", 4096);

    auto hstock = NewFromPool<struct hstock>(*pool);
    hstock->pool = pool;
    hstock->cls = cls;
    hstock->class_ctx = class_ctx;
    hstock->limit = limit;
    hstock->max_idle = max_idle;
    hstock->stocks = hashmap_new(pool, 64);

    return hstock;
}

void
hstock_free(struct hstock *hstock)
{
    const struct hashmap_pair *pair;

    assert(hstock != nullptr);

    hashmap_rewind(hstock->stocks);

    while ((pair = hashmap_next(hstock->stocks)) != nullptr) {
        Stock *stock = (Stock *)pair->value;

        stock_free(stock);
    }

    pool_unref(hstock->pool);
}

void
hstock_fade_all(struct hstock &hstock)
{
    hashmap_rewind(hstock.stocks);

    const struct hashmap_pair *pair;
    while ((pair = hashmap_next(hstock.stocks)) != nullptr) {
        Stock &stock = *(Stock *)pair->value;
        stock_fade_all(stock);
    }
}

void
hstock_add_stats(const struct hstock *stock, StockStats &data)
{
    struct hashmap *h = stock->stocks;
    hashmap_rewind(h);

    const struct hashmap_pair *p;
    while ((p = hashmap_next(h)) != nullptr) {
        const Stock &s = *(const Stock *)p->value;
        stock_add_stats(s, data);
    }
}

static Stock &
hstock_get_stock(struct hstock *hstock, const char *uri)
{
    assert(hstock != nullptr);

    Stock *stock = (Stock *)hashmap_get(hstock->stocks, uri);
    if (stock == nullptr) {
        stock = stock_new(*hstock->pool, *hstock->cls, hstock->class_ctx,
                          uri, hstock->limit, hstock->max_idle,
                          hstock_stock_handler, hstock);
        hashmap_set(hstock->stocks, stock_get_uri(*stock), stock);
    }

    return *stock;
}

void
hstock_get(struct hstock *hstock, struct pool *pool,
           const char *uri, void *info,
           const StockGetHandler &handler, void *handler_ctx,
           struct async_operation_ref &async_ref)
{
    assert(hstock != nullptr);

    auto &stock = hstock_get_stock(hstock, uri);
    stock_get(stock, *pool, info, handler, handler_ctx, async_ref);
}

StockItem *
hstock_get_now(struct hstock *hstock, struct pool *pool,
               const char *uri, void *info,
               GError **error_r)
{
    assert(hstock != nullptr);

    Stock &stock = hstock_get_stock(hstock, uri);
    return stock_get_now(stock, *pool, info, error_r);
}

void
hstock_put(struct hstock *hstock gcc_unused, const char *uri gcc_unused,
           StockItem &object, bool destroy)
{
#ifndef NDEBUG
    Stock *stock = (Stock *)hashmap_get(hstock->stocks, uri);

    assert(stock != nullptr);
    assert(stock == object.stock);
#endif

    stock_put(object, destroy);
}
