/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Item.hxx"
#include "Stock.hxx"
#include "pool.hxx"

const char *
CreateStockItem::GetStockName() const
{
    return stock.GetUri();
}

void
CreateStockItem::InvokeCreateError(GError *error)
{
    stock.ItemCreateError(handler, error);
}

void
CreateStockItem::InvokeCreateAborted()
{
    stock.ItemCreateAborted();
}

StockItem::~StockItem()
{
}

const char *
StockItem::GetStockName() const
{
    return stock.GetUri();
}

void
StockItem::Put(bool destroy)
{
    stock.Put(*this, destroy);
}

void
StockItem::InvokeCreateSuccess()
{
    stock.ItemCreateSuccess(*this);
}

void
StockItem::InvokeCreateError(GError *error)
{
    stock.ItemCreateError(*this, error);
}

void
StockItem::InvokeCreateAborted()
{
    stock.ItemCreateAborted(*this);
}

void
StockItem::InvokeIdleDisconnect()
{
    stock.ItemIdleDisconnect(*this);
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
