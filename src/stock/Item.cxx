/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Item.hxx"
#include "Stock.hxx"
#include "pool.hxx"

StockItem::~StockItem()
{
}

void
StockItem::Put(bool destroy)
{
    stock_put(*this, destroy);
}

void
StockItem::InvokeCreateSuccess()
{
    stock_item_available(*this);
}

void
StockItem::InvokeCreateError(GError *error)
{
    stock_item_failed(*this, error);
}

void
StockItem::InvokeCreateAborted()
{
    stock_item_aborted(*this);
}

void
StockItem::InvokeIdleDisconnect()
{
    stock_del(*this);
}

void
HeapStockItem::Destroy(gcc_unused void *ctx)
{
    delete this;
}

void
PoolStockItem::Destroy(gcc_unused void *ctx)
{
    assert(pool_contains(&pool, this, sizeof(*this)));

    DeleteUnrefPool(pool, this);
}
