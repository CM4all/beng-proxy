/*
 * The StockMap class is a hash table of any number of Stock objects,
 * each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MAP_STOCK_HXX
#define BENG_PROXY_MAP_STOCK_HXX

#include "Stock.hxx"

#include <inline/compiler.h>

#include <boost/intrusive/unordered_set.hpp>

class StockMap final : StockHandler {
    struct Item
        : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        Stock stock;

        template<typename... Args>
        explicit Item(Args&&... args):stock(std::forward<Args>(args)...) {}

        static Item &Cast(Stock &s) {
            return ContainerCast2(s, &Item::stock);
        }

        const char *GetKey() const {
            return stock.GetName();
        }

        gcc_pure
        static size_t KeyHasher(const char *key);

        gcc_pure
        static size_t ValueHasher(const Item &value) {
            return KeyHasher(value.GetKey());
        }

        gcc_pure
        static bool KeyValueEqual(const char *a, const Item &b) {
            assert(a != nullptr);

            return strcmp(a, b.GetKey()) == 0;
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
                return KeyValueEqual(a.GetKey(), b);
            }
        };
    };

    typedef boost::intrusive::unordered_set<Item,
                                            boost::intrusive::hash<Item::Hash>,
                                            boost::intrusive::equal<Item::Equal>,
                                            boost::intrusive::constant_time_size<false>> Map;

    EventLoop &event_loop;

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

public:
    StockMap(EventLoop &_event_loop, const StockClass &_cls, void *_class_ctx,
             unsigned _limit, unsigned _max_idle)
        :event_loop(_event_loop), cls(_cls), class_ctx(_class_ctx),
         limit(_limit), max_idle(_max_idle),
         map(Map::bucket_traits(buckets, N_BUCKETS)) {}

    ~StockMap();

    EventLoop &GetEventLoop() {
        return event_loop;
    }

    void *GetClassContext() {
        return class_ctx;
    }

    void Erase(Item &item);

    /**
     * @see Stock::FadeAll()
     */
    void FadeAll() {
        for (auto &i : map)
            i.stock.FadeAll();
    }

    /**
     * Obtain statistics.
     */
    void AddStats(StockStats &data) const {
        for (const auto &i : map)
            i.stock.AddStats(data);
    }

    Stock &GetStock(const char *uri);

    void Get(struct pool &caller_pool,
             const char *uri, void *info,
             StockGetHandler &handler,
             struct async_operation_ref &async_ref) {
        Stock &stock = GetStock(uri);
        stock.Get(caller_pool, info, handler, async_ref);
    }

    /**
     * Obtains an item from the stock without going through the
     * callback.  This requires a stock class which finishes the
     * create() method immediately.
     */
    StockItem *GetNow(struct pool &caller_pool, const char *uri, void *info,
                      GError **error_r) {
        Stock &stock = GetStock(uri);
        return stock.GetNow(caller_pool, info, error_r);
    }

    /* virtual methods from class StockHandler */
    void OnStockEmpty(Stock &stock) override;
};

#endif
