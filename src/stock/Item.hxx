/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_ITEM_HXX
#define BENG_PROXY_STOCK_ITEM_HXX

#include "glibfwd.hxx"

#include <boost/intrusive/list.hpp>

struct pool;
struct Stock;
class StockGetHandler;

struct CreateStockItem {
    Stock &stock;
    struct pool &pool;
    StockGetHandler &handler;
};

struct StockItem
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    Stock &stock;

    StockGetHandler &handler;

    /**
     * If true, then this object will never be reused.
     */
    bool fade = false;

#ifndef NDEBUG
    bool is_idle = false;
#endif

    explicit StockItem(CreateStockItem c)
        :stock(c.stock), handler(c.handler) {}

    virtual ~StockItem();

    /**
     * Return a busy item to the stock.  This is a wrapper for
     * Stock::Put().
     */
    void Put(bool destroy);

    /**
     * Prepare this item to be borrowed by a client.
     *
     * @return false when this item is defunct and shall be destroyed
     */
    virtual bool Borrow(void *ctx) = 0;

    /**
     * Return this borrowed item into the "idle" list.
     *
     * @return false when this item is defunct and shall not be reused
     * again; it will be destroyed by the caller
     */
    virtual bool Release(void *ctx) = 0;

    /**
     * Ask the item to destroy itself after it has been returned and
     * removed from all lists.  The item may be good or bad at this
     * point.
     */
    virtual void Destroy(void *ctx) = 0;

    /**
     * Announce that the creation of this item has finished
     * successfully, and it is ready to be used.
     */
    void InvokeCreateSuccess();

    /**
     * Announce that the creation of this item has failed.
     */
    void InvokeCreateError(GError *error);

    /**
     * Announce that the creation of this item has been aborted by the
     * caller.
     */
    void InvokeCreateAborted();

    /**
     * Announce that the item has been disconnected by the peer while
     * it was idle.
     */
    void InvokeIdleDisconnect();
};

struct HeapStockItem : StockItem {
    explicit HeapStockItem(CreateStockItem c):StockItem(c) {}

    void Destroy(void *ctx) override;
};

struct PoolStockItem : StockItem {
    struct pool &pool;

    explicit PoolStockItem(CreateStockItem c)
        :StockItem(c), pool(c.pool) {}

    void Destroy(void *ctx) override;
};

#endif
