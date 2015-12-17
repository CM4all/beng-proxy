/*
 * Objects in stock.  May be used for connection pooling.
 *
 * The 'stock' class holds a number of idle objects.  The URI may be
 * something like a hostname:port pair for HTTP client connections -
 * it is not used by this class, but passed to the stock_class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_HXX
#define BENG_PROXY_STOCK_HXX

#include "Item.hxx"
#include "Stats.hxx"
#include "event/TimerEvent.hxx"
#include "event/DeferEvent.hxx"
#include "async.hxx"

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct async_operation_ref;
struct Stock;
struct StockClass;
struct CreateStockItem;
class StockGetHandler;

class StockHandler {
public:
    /**
     * The stock has become empty.  It is safe to delete it from
     * within this method.
     */
    virtual void OnStockEmpty(Stock &stock, const char *uri) = 0;
};

class Stock {
    struct pool &pool;
    const StockClass &cls;
    void *const class_ctx;
    const char *const uri;

    /**
     * The maximum number of items in this stock.  If any more items
     * are requested, they are put into the #waiting list, which gets
     * checked as soon as Put() is called.
     */
    const unsigned limit;

    /**
     * The maximum number of permanent idle items.  If there are more
     * than that, a timer will incrementally kill excess items.
     */
    const unsigned max_idle;

    StockHandler *const handler;

    /**
     * This event is used to move the "retry waiting" code out of the
     * current stack, to invoke the handler method in a safe
     * environment.
     */
    DeferEvent retry_event;

    /**
     * This event is used to move the "empty" check out of the current
     * stack, to invoke the handler method in a safe environment.
     */
    DeferEvent empty_event;

    TimerEvent cleanup_event;
    TimerEvent clear_event;

    typedef boost::intrusive::list<StockItem,
                                   boost::intrusive::constant_time_size<true>> ItemList;

    ItemList idle;

    ItemList busy;

    unsigned num_create = 0;

    struct Waiting
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

        Stock &stock;

        struct async_operation operation;

        struct pool &pool;
        void *const info;

        StockGetHandler &handler;

        struct async_operation_ref &async_ref;

        Waiting(Stock &_stock, struct pool &_pool, void *_info,
                StockGetHandler &_handler,
                struct async_operation_ref &_async_ref);

        void Destroy();

        void Abort();
    };

    typedef boost::intrusive::list<Waiting,
                                   boost::intrusive::constant_time_size<false>> WaitingList;

    WaitingList waiting;

    bool may_clear = false;

public:
    Stock(struct pool &_pool, const StockClass &cls, void *class_ctx,
          const char *uri, unsigned limit, unsigned max_idle,
          StockHandler *handler=nullptr);

    ~Stock();

    const char *GetUri() const {
        return uri;
    }

    /**
     * Returns true if there are no items in the stock - neither idle
     * nor busy.
     */
    gcc_pure
    bool IsEmpty() const {
        return idle.empty() && busy.empty() && num_create == 0;
    }

    /**
     * Obtain statistics.
     */
    void AddStats(StockStats &data) const {
        data.busy += busy.size();
        data.idle += idle.size();
    }

    /**
     * Destroy all idle items and don't reuse any of the current busy
     * items.
     */
    void FadeAll();

private:
    /**
     * Check if the stock has become empty, and invoke the handler.
     */
    void CheckEmpty();
    void ScheduleCheckEmpty();

    void DestroyItem(StockItem &item);

    void ScheduleClear() {
        static constexpr struct timeval tv = { .tv_sec = 60, .tv_usec = 0 };
        clear_event.Add(tv);
    }

    void ClearIdle();

    bool GetIdle(StockGetHandler &handler);
    void GetCreate(struct pool &caller_pool, void *info,
                   StockGetHandler &get_handler,
                   struct async_operation_ref &async_ref);

public:
    void Get(struct pool &caller_pool, void *info,
             StockGetHandler &get_handler,
             struct async_operation_ref &async_ref);

    /**
     * Obtains an item from the stock without going through the
     * callback.  This requires a stock class which finishes the
     * create() method immediately.
     */
    StockItem *GetNow(struct pool &caller_pool, void *info, GError **error_r);

    void Put(StockItem &item, bool destroy);

    void ItemIdleDisconnect(StockItem &item);

    void ItemCreateSuccess(StockItem &item);
    void ItemCreateError(StockItem &item, GError *error);
    void ItemCreateAborted(StockItem &item);

    /**
     * Retry the waiting requests.  This is called after the number of
     * busy items was reduced.
     */
    void RetryWaiting();
    void ScheduleRetryWaiting();

private:
    void ScheduleCleanup() {
        static constexpr struct timeval tv = { .tv_sec = 20, .tv_usec = 0 };
        cleanup_event.Add(tv);
    }

    void UnscheduleCleanup() {
        cleanup_event.Cancel();
    }
    void CleanupEventCallback();
    void ClearEventCallback();
};

#endif
