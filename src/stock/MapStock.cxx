/*
 * The StockMap class is a hash table of any number of Stock objects,
 * each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "MapStock.hxx"
#include "util/djbhash.h"
#include "util/DeleteDisposer.hxx"

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

    logger.Format(5, "hstock(%p) remove empty stock(%p, '%s')",
                  (const void *)this, (const void *)&stock, stock.GetName());

    Erase(item);
}

Stock &
StockMap::GetStock(const char *uri)
{
    Map::insert_commit_data hint;
    auto i = map.insert_check(uri, Item::KeyHasher, Item::KeyValueEqual, hint);
    if (i.second) {
        auto *item = new Item(event_loop, cls, class_ctx,
                              uri, limit, max_idle,
                              this);
        map.insert_commit(*item, hint);
        return item->stock;
    } else
        return i.first->stock;

}
