/*
 * The StockMap class is a hash table of any number of Stock objects,
 * each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hstock.hxx"
#include "stock.hxx"
#include "hashmap.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>

struct StockMap {
    struct pool &pool;
    const StockClass &cls;
    void *const class_ctx;

    /**
     * The maximum number of items in each stock.
     */
    const unsigned limit;

    /**
     * The maximum number of permanent idle items in each stock.
     */
    const unsigned max_idle;

    struct hashmap &stocks;

    StockMap(struct pool &_pool, const StockClass &_cls, void *_class_ctx,
             unsigned _limit, unsigned _max_idle)
        :pool(*pool_new_libc(&_pool, "hstock")),
         cls(_cls), class_ctx(_class_ctx),
         limit(_limit), max_idle(_max_idle),
         stocks(*hashmap_new(&pool, 64)) {}

    ~StockMap() {
        hashmap_rewind(&stocks);
        const struct hashmap_pair *pair;
        while ((pair = hashmap_next(&stocks)) != nullptr) {
            Stock *stock = (Stock *)pair->value;

            stock_free(stock);
        }
    }

    void Destroy() {
        DeleteUnrefPool(pool, this);
    }

    void Erase(Stock &stock, const char *uri) {
        hashmap_remove_existing(&stocks, uri, &stock);
        stock_free(&stock);
    }

    void FadeAll() {
        hashmap_rewind(&stocks);

        const struct hashmap_pair *pair;
        while ((pair = hashmap_next(&stocks)) != nullptr) {
            Stock &stock = *(Stock *)pair->value;
            stock_fade_all(stock);
        }
    }

    void AddStats(StockStats &data) const {
        hashmap_rewind(&stocks);

        const struct hashmap_pair *p;
        while ((p = hashmap_next(&stocks)) != nullptr) {
            const Stock &s = *(const Stock *)p->value;
            stock_add_stats(s, data);
        }
    }

    Stock &GetStock(const char *uri);

    void Get(struct pool &caller_pool,
             const char *uri, void *info,
             const StockGetHandler &handler, void *handler_ctx,
             struct async_operation_ref &async_ref) {
        Stock &stock = GetStock(uri);
        stock_get(stock, caller_pool, info, handler, handler_ctx, async_ref);
    }

    StockItem *GetNow(struct pool &caller_pool, const char *uri, void *info,
                      GError **error_r) {
        Stock &stock = GetStock(uri);
        return stock_get_now(stock, caller_pool, info, error_r);
    }

    void Put(gcc_unused const char *uri, StockItem &object, bool destroy) {
#ifndef NDEBUG
        Stock *stock = (Stock *)hashmap_get(&stocks, uri);

        assert(stock != nullptr);
        assert(stock == object.stock);
#endif

        stock_put(object, destroy);
    }
};

/*
 * stock handler
 *
 */

static void
hstock_stock_empty(Stock &stock, const char *uri, void *ctx)
{
    StockMap *hstock = (StockMap *)ctx;

    daemon_log(5, "hstock(%p) remove empty stock(%p, '%s')\n",
               (const void *)hstock, (const void *)&stock, uri);

    hstock->Erase(stock, uri);
}

static constexpr StockHandler hstock_stock_handler = {
    .empty = hstock_stock_empty,
};

StockMap *
hstock_new(struct pool &pool, const StockClass &cls, void *class_ctx,
           unsigned limit, unsigned max_idle)
{
    assert(cls.item_size > sizeof(StockItem));
    assert(cls.create != nullptr);
    assert(cls.borrow != nullptr);
    assert(cls.release != nullptr);
    assert(cls.destroy != nullptr);
    assert(max_idle > 0);

    return NewFromPool<StockMap>(pool, pool, cls, class_ctx,
                                 limit, max_idle);
}

void
hstock_free(StockMap *hstock)
{
    hstock->Destroy();
}

void
hstock_fade_all(StockMap &hstock)
{
    hstock.FadeAll();
}

void
hstock_add_stats(const StockMap &stock, StockStats &data)
{
    stock.AddStats(data);
}

inline Stock &
StockMap::GetStock(const char *uri)
{
    Stock *stock = (Stock *)hashmap_get(&stocks, uri);
    if (stock == nullptr) {
        stock = stock_new(pool, cls, class_ctx,
                          uri, limit, max_idle,
                          hstock_stock_handler, this);
        hashmap_set(&stocks, stock_get_uri(*stock), stock);
    }

    return *stock;
}

void
hstock_get(StockMap &hstock, struct pool &pool,
           const char *uri, void *info,
           const StockGetHandler &handler, void *handler_ctx,
           struct async_operation_ref &async_ref)
{
    return hstock.Get(pool, uri, info,
                      handler, handler_ctx, async_ref);
}

StockItem *
hstock_get_now(StockMap &hstock, struct pool &pool,
               const char *uri, void *info,
               GError **error_r)
{
    return hstock.GetNow(pool, uri, info, error_r);
}

void
hstock_put(StockMap &hstock, const char *uri, StockItem &object, bool destroy)
{
    hstock.Put(uri, object, destroy);
}
