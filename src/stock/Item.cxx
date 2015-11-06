/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Item.hxx"
#include "pool.hxx"

StockItem::~StockItem()
{
}

void
StockItem::Destroy(gcc_unused void *ctx)
{
    DeleteUnrefPool(pool, this);
}
