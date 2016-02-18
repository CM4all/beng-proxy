/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_stock.hxx"
#include "lhttp_quark.hxx"
#include "lhttp_address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/MultiStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "child_stock.hxx"
#include "child_manager.hxx"
#include "spawn/Prepared.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>

struct LhttpStock {
    StockMap *const hstock;
    MultiStock *const child_stock;

    LhttpStock(struct pool &pool, unsigned limit, unsigned max_idle);

    ~LhttpStock() {
        hstock_free(hstock);
        mstock_free(child_stock);
    }

    void FadeAll() {
        hstock_fade_all(*hstock);
        mstock_fade_all(*child_stock);
    }
};

struct LhttpConnection final : HeapStockItem {
    StockItem *child = nullptr;

    struct lease_ref lease_ref;

    int fd = -1;
    Event event;

    explicit LhttpConnection(CreateStockItem c)
        :HeapStockItem(c) {}

    ~LhttpConnection() override;

    void EventCallback(evutil_socket_t fd, short events);

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        static constexpr struct timeval tv = {
            .tv_sec = 300,
            .tv_usec = 0,
        };

        event.Add(tv);
        return true;
    }
};

static const char *
lhttp_stock_key(struct pool *pool, const LhttpAddress *address)
{
    return address->GetServerId(pool);
}

/*
 * libevent callback
 *
 */

inline void
LhttpConnection::EventCallback(evutil_socket_t _fd, short events)
{
    assert(_fd == fd);

    if ((events & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(_fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle LHTTP connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle LHTTP connection\n");
    }

    InvokeIdleDisconnect();
    pool_commit();
}

/*
 * child_stock class
 *
 */

static int
lhttp_child_stock_socket_type(void *info)
{
    const auto &address = *(const LhttpAddress *)info;

    int type = SOCK_STREAM;
    if (!address.blocking)
        type |= SOCK_NONBLOCK;

    return type;
}

static bool
lhttp_child_stock_prepare(void *info, int fd,
                          PreparedChildProcess &p, GError **error_r)
{
    const auto &address = *(const LhttpAddress *)info;

    p.stdin_fd = fd;

    return address.CopyTo(p, error_r);
}

static const ChildStockClass lhttp_child_stock_class = {
    .shutdown_signal = SIGTERM,
    .socket_type = lhttp_child_stock_socket_type,
    .prepare = lhttp_child_stock_prepare,
};

/*
 * stock class
 *
 */

static void
lhttp_stock_create(void *ctx, gcc_unused struct pool &parent_pool,
                   CreateStockItem c, void *info,
                   struct pool &caller_pool,
                   gcc_unused struct async_operation_ref &async_ref)
{
    auto lhttp_stock = (LhttpStock *)ctx;
    const auto *address = (const LhttpAddress *)info;

    assert(address != nullptr);
    assert(address->path != nullptr);

    auto *connection = new LhttpConnection(c);

    const char *key = c.GetStockName();

    GError *error = nullptr;
    connection->child = mstock_get_now(*lhttp_stock->child_stock, caller_pool,
                                       key, info, address->concurrency,
                                       connection->lease_ref,
                                       &error);
    if (connection->child == nullptr) {
        g_prefix_error(&error, "failed to launch LHTTP server '%s': ", key);
        connection->InvokeCreateError(error);
        return;
    }

    connection->fd = child_stock_item_connect(connection->child, &error);

    if (connection->fd < 0) {
        g_prefix_error(&error, "failed to connect to LHTTP server '%s': ",
                       key);
        connection->lease_ref.Release(false);
        connection->InvokeCreateError(error);
        return;
    }

    connection->event.Set(connection->fd, EV_READ|EV_TIMEOUT,
                          MakeEventCallback(LhttpConnection, EventCallback),
                          connection);

    connection->InvokeCreateSuccess();
}

LhttpConnection::~LhttpConnection()
{
    if (fd >= 0) {
        event.Delete();
        close(fd);
    }

    if (child != nullptr)
        lease_ref.Release(true);
}

static constexpr StockClass lhttp_stock_class = {
    .create = lhttp_stock_create,
};


/*
 * interface
 *
 */

inline
LhttpStock::LhttpStock(struct pool &pool, unsigned limit, unsigned max_idle)
    :hstock(hstock_new(pool, lhttp_stock_class, this, limit, max_idle)),
     child_stock(mstock_new(*child_stock_new(&pool, limit, max_idle,
                                             &lhttp_child_stock_class))) {}

LhttpStock *
lhttp_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    return new LhttpStock(*pool, limit, max_idle);
}

void
lhttp_stock_free(LhttpStock *ls)
{
    delete ls;
}

void
lhttp_stock_fade_all(LhttpStock &ls)
{
    ls.FadeAll();
}

StockItem *
lhttp_stock_get(LhttpStock *lhttp_stock, struct pool *pool,
                const LhttpAddress *address,
                GError **error_r)
{
    const auto *const jail = &address->options.jail;
    if (jail->enabled && jail->home_directory == nullptr) {
        g_set_error(error_r, lhttp_quark(), 0,
                    "No home directory for jailed LHTTP");
        return nullptr;
    }

    union {
        const LhttpAddress *in;
        void *out;
    } deconst = { .in = address };

    return hstock_get_now(*lhttp_stock->hstock, *pool,
                          lhttp_stock_key(pool, address),
                          deconst.out, error_r);
}

int
lhttp_stock_item_get_socket(const StockItem &item)
{
    const auto *connection = (const LhttpConnection *)&item;

    assert(connection->fd >= 0);

    return connection->fd;
}

FdType
lhttp_stock_item_get_type(gcc_unused const StockItem &item)
{
    return FdType::FD_SOCKET;
}
