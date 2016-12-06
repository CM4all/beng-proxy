/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_LEASE_HXX
#define BENG_PROXY_STOCK_LEASE_HXX

#include "lease.hxx"

struct StockItem;

/**
 * A #Lease implementation which calls StockItem::Put().
 */
class StockItemLease final : public Lease {
    StockItem &item;

public:
    explicit StockItemLease(StockItem &_item):item(_item) {}

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override;
};

#endif
