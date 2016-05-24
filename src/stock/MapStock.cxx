/*
 * The StockMap class is a hash table of any number of Stock objects,
 * each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "MapStock.hxx"
#include "util/djbhash.h"
#include "util/DeleteDisposer.hxx"

#include <daemon/log.h>

#include <assert.h>

inline size_t
StockMap::Item::KeyHasher(const char *key)
{
    assert(key != nullptr);

    return djb_hash_string(key);
}

StockMap::~StockMap()
{
    map.clear_and_dispose(DeleteDisposer());
}

void
StockMap::Erase(Item &item)
{
    auto i = map.iterator_to(item);
    map.erase_and_dispose(i, DeleteDisposer());
}

void
StockMap::OnStockEmpty(Stock &stock)
{
    auto &item = Item::Cast(stock);

    daemon_log(5, "hstock(%p) remove empty stock(%p, '%s')\n",
               (const void *)this, (const void *)&stock, stock.GetName());

    Erase(item);
}

StockMap *
hstock_new(const StockClass &cls, void *class_ctx,
           unsigned limit, unsigned max_idle)
{
    assert(max_idle > 0);

    return new StockMap(cls, class_ctx,
                        limit, max_idle);
}

void
hstock_free(StockMap *hstock)
{
    delete hstock;
}

void *
hstock_get_ctx(StockMap &hstock)
{
    return hstock.class_ctx;
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
        auto *item = new Item(cls, class_ctx,
                              uri, limit, max_idle,
                              this);
        map.insert_commit(*item, hint);
        return item->stock;
    } else
        return i.first->stock;

}

void
hstock_get(StockMap &hstock, struct pool &pool,
           const char *uri, void *info,
           StockGetHandler &handler,
           struct async_operation_ref &async_ref)
{
    return hstock.Get(pool, uri, info, handler, async_ref);
}

StockItem *
hstock_get_now(StockMap &hstock, struct pool &pool,
               const char *uri, void *info,
               GError **error_r)
{
    return hstock.GetNow(pool, uri, info, error_r);
}
