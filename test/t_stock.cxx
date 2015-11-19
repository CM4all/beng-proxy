#include "stock/Stock.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "async.hxx"
#include "pool.hxx"

#include <glib.h>

#include <event.h>
#include <assert.h>

static unsigned num_create, num_fail, num_borrow, num_release, num_destroy;
static bool next_fail;
static bool got_item;
static StockItem *last_item;

struct MyStockItem final : PoolStockItem {
    void *info;

    explicit MyStockItem(CreateStockItem c)
        :PoolStockItem(c) {}

    ~MyStockItem() override {
        ++num_destroy;
    }

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        ++num_borrow;
        return true;
    }

    void Release(gcc_unused void *ctx) override {
        ++num_release;
    }
};

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

/*
 * stock class
 *
 */

static struct pool *
my_stock_pool(gcc_unused void *ctx, struct pool &parent,
              gcc_unused const char *uri)
{
    return pool_new_linear(&parent, "my_stock", 512);
}

static void
my_stock_create(void *ctx gcc_unused, CreateStockItem c,
                gcc_unused const char *uri, void *info,
                gcc_unused struct pool &caller_pool,
                gcc_unused struct async_operation_ref &async_ref)
{
    auto *item = NewFromPool<MyStockItem>(c.pool, c);

    item->info = info;

    if (next_fail) {
        ++num_fail;

        GError *error = g_error_new_literal(test_quark(), 0, "next_fail");
        stock_item_failed(*item, error);
    } else {
        ++num_create;
        stock_item_available(*item);
    }
}

static constexpr StockClass my_stock_class = {
    .pool = my_stock_pool,
    .create = my_stock_create,
};

class MyStockGetHandler final : public StockGetHandler {
public:
    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override {
        assert(!got_item);

        got_item = true;
        last_item = &item;
    }

    void OnStockItemError(GError *error) override {
        g_printerr("%s\n", error->message);
        g_error_free(error);

        got_item = true;
        last_item = nullptr;
    }
};

int main(gcc_unused int argc, gcc_unused char **argv)
{
    struct event_base *event_base;
    struct pool *pool;
    Stock *stock;
    struct async_operation_ref async_ref;
    StockItem *item, *second, *third;

    event_base = event_init();
    pool = pool_new_libc(nullptr, "root");

    stock = stock_new(*pool, my_stock_class, nullptr, nullptr, 3, 8,
                      nullptr, nullptr);

    MyStockGetHandler handler;

    /* create first item */

    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(got_item);
    assert(last_item != nullptr);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 0 && num_release == 0 && num_destroy == 0);
    item = last_item;

    /* release first item */

    stock_put(*item, false);
    event_loop(EVLOOP_NONBLOCK);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 0 && num_release == 1 && num_destroy == 0);

    /* reuse first item */

    got_item = false;
    last_item = nullptr;
    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(got_item);
    assert(last_item == item);
    assert(num_create == 1 && num_fail == 0);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);

    /* create second item */

    got_item = false;
    last_item = nullptr;
    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(got_item);
    assert(last_item != nullptr);
    assert(last_item != item);
    assert(num_create == 2 && num_fail == 0);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 0);
    second = last_item;

    /* fail to create third item */

    next_fail = true;
    got_item = false;
    last_item = nullptr;
    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(got_item);
    assert(last_item == nullptr);
    assert(num_create == 2 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);

    /* create third item */

    next_fail = false;
    got_item = false;
    last_item = nullptr;
    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(got_item);
    assert(last_item != nullptr);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);
    third = last_item;

    /* fourth item waiting */

    got_item = false;
    last_item = nullptr;
    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(!got_item);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);

    /* fifth item waiting */

    stock_get(*stock, *pool, nullptr, handler, async_ref);
    assert(!got_item);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 1 && num_release == 1 && num_destroy == 1);

    /* return third item */

    stock_put(*third, false);
    event_loop(EVLOOP_NONBLOCK);
    assert(num_create == 3 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 1);
    assert(got_item);
    assert(last_item == third);

    /* destroy second item */

    got_item = false;
    last_item = nullptr;
    stock_put(*second, true);
    event_loop(EVLOOP_NONBLOCK);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 2);
    assert(got_item);
    assert(last_item != nullptr);
    second = last_item;

    /* destroy first item */

    stock_put(*item, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 3);

    /* destroy second item */

    stock_put(*second, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 4);

    /* destroy third item */

    stock_put(*third, true);
    assert(num_create == 4 && num_fail == 1);
    assert(num_borrow == 2 && num_release == 2 && num_destroy == 5);

    /* cleanup */

    stock_free(stock);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
