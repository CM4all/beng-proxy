/*
 * The StockMap class is a hash table of any number of Stock objects,
 * each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hstock.hxx"
#include "stock.hxx"
#include "pool.hxx"
#include "util/djbhash.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

#include <boost/intrusive/unordered_set.hpp>

struct StockMap {
    struct Item
        : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        const char *const uri;

        Stock &stock;

        Item(const char *_uri, Stock &_stock):uri(_uri), stock(_stock) {}

        Item(const Item &) = delete;

        ~Item() {
            stock_free(&stock);
        }

        gcc_pure
        static size_t KeyHasher(const char *key) {
            assert(key != nullptr);

            return djb_hash_string(key);
        }

        gcc_pure
        static size_t ValueHasher(const Item &value) {
            return KeyHasher(value.uri);
        }

        gcc_pure
        static bool KeyValueEqual(const char *a, const Item &b) {
            assert(a != nullptr);

            return strcmp(a, b.uri) == 0;
        }

        struct Hash {
            gcc_pure
            size_t operator()(const Item &value) const {
                return ValueHasher(value);
            }
        };

        struct Equal {
            gcc_pure
            bool operator()(const Item &a, const Item &b) const {
                return KeyValueEqual(a.uri, b);
            }
        };

        struct Disposer {
            void operator()(Item *item) const {
                delete item;
            }
        };
    };

    typedef boost::intrusive::unordered_set<Item,
                                            boost::intrusive::hash<Item::Hash>,
                                            boost::intrusive::equal<Item::Equal>,
                                            boost::intrusive::constant_time_size<false>> Map;

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

    Map map;

    static constexpr size_t N_BUCKETS = 251;
    Map::bucket_type buckets[N_BUCKETS];

    StockMap(struct pool &_pool, const StockClass &_cls, void *_class_ctx,
             unsigned _limit, unsigned _max_idle)
        :pool(*pool_new_libc(&_pool, "hstock")),
         cls(_cls), class_ctx(_class_ctx),
         limit(_limit), max_idle(_max_idle),
         map(Map::bucket_traits(buckets, N_BUCKETS)) {}

    ~StockMap() {
        map.clear_and_dispose(Item::Disposer());
    }

    void Destroy() {
        DeleteUnrefPool(pool, this);
    }

    void Erase(gcc_unused Stock &stock, const char *uri) {
#ifndef NDEBUG
        auto i = map.find(uri, Item::KeyHasher, Item::KeyValueEqual);
        assert(i != map.end());
        assert(&i->stock == &stock);
#endif

        map.erase_and_dispose(uri, Item::KeyHasher, Item::KeyValueEqual,
                              Item::Disposer());
    }

    void FadeAll() {
        for (auto &i : map)
            stock_fade_all(i.stock);
    }

    void AddStats(StockStats &data) const {
        for (const auto &i : map)
            stock_add_stats(i.stock, data);
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
        auto i = map.find(uri, Item::KeyHasher, Item::KeyValueEqual);
        assert(i != map.end());
        assert(&i->stock == object.stock);
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
    Map::insert_commit_data hint;
    auto i = map.insert_check(uri, Item::KeyHasher, Item::KeyValueEqual, hint);
    if (i.second) {
        auto *stock = stock_new(pool, cls, class_ctx,
                                uri, limit, max_idle,
                                hstock_stock_handler, this);
        map.insert_commit(*new Item(stock_get_uri(*stock), *stock), hint);
        return *stock;
    } else
        return i.first->stock;

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
