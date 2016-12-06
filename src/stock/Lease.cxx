/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Lease.hxx"
#include "Item.hxx"

void
StockItemLease::ReleaseLease(bool reuse)
{
    item.Put(!reuse);
}
