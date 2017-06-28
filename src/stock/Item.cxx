/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Item.hxx"
#include "Stock.hxx"

const char *
CreateStockItem::GetStockName() const
{
    return stock.GetName();
}

void
CreateStockItem::InvokeCreateError(std::exception_ptr ep)
{
    stock.ItemCreateError(handler, ep);
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
    return stock.GetName();
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
StockItem::InvokeCreateError(std::exception_ptr ep)
{
    stock.ItemCreateError(*this, ep);
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
