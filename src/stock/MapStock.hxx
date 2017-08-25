/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_MAP_STOCK_HXX
#define BENG_PROXY_MAP_STOCK_HXX

#include "Stock.hxx"
#include "io/Logger.hxx"
#include "util/Cast.hxx"
#include "util/Compiler.h"

#include <boost/intrusive/unordered_set.hpp>

/**
 * A hash table of any number of Stock objects, each with a different
 * URI.
 */
class StockMap final : StockHandler {
    struct Item
        : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        Stock stock;

        template<typename... Args>
        explicit Item(Args&&... args):stock(std::forward<Args>(args)...) {}

        static Item &Cast(Stock &s) {
            return ContainerCast(s, &Item::stock);
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

    const Logger logger;

    EventLoop &event_loop;

    StockClass &cls;

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
    StockMap(EventLoop &_event_loop, StockClass &_cls,
             unsigned _limit, unsigned _max_idle)
        :event_loop(_event_loop), cls(_cls),
         limit(_limit), max_idle(_max_idle),
         map(Map::bucket_traits(buckets, N_BUCKETS)) {}

    ~StockMap();

    EventLoop &GetEventLoop() {
        return event_loop;
    }

    StockClass &GetClass() {
        return cls;
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
             CancellablePointer &cancel_ptr) {
        Stock &stock = GetStock(uri);
        stock.Get(caller_pool, info, handler, cancel_ptr);
    }

    /**
     * Obtains an item from the stock without going through the
     * callback.  This requires a stock class which finishes the
     * create() method immediately.
     *
     * Throws exception on error.
     */
    StockItem *GetNow(struct pool &caller_pool, const char *uri, void *info) {
        Stock &stock = GetStock(uri);
        return stock.GetNow(caller_pool, info);
    }

    /* virtual methods from class StockHandler */
    void OnStockEmpty(Stock &stock) override;
};

#endif
