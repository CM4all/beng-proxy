/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Item.hxx"
#include "pool.hxx"

StockItem::~StockItem()
{
}

void
PoolStockItem::Destroy(gcc_unused void *ctx)
{
    assert(pool_contains(&pool, this, sizeof(*this)));

    DeleteUnrefPool(pool, this);
}
