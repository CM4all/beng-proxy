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
    StockMap hstock;
    StockMap *const child_stock;
    MultiStock *const mchild_stock;

    LhttpStock(unsigned limit, unsigned max_idle,
               SpawnService &spawn_service);

    ~LhttpStock() {
        mstock_free(mchild_stock);
        child_stock_free(child_stock);
    }

    void FadeAll() {
        hstock.FadeAll();
        child_stock->FadeAll();
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
        event.Add(EventDuration<300>::value);
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
    .socket_type = lhttp_child_stock_socket_type,
    .prepare = lhttp_child_stock_prepare,
};

/*
 * stock class
 *
 */

static void
lhttp_stock_create(void *ctx, CreateStockItem c, void *info,
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
    connection->child = mstock_get_now(*lhttp_stock->mchild_stock, caller_pool,
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
LhttpStock::LhttpStock(unsigned limit, unsigned max_idle,
                       SpawnService &spawn_service)
    :hstock(lhttp_stock_class, this, limit, max_idle),
     child_stock(child_stock_new(limit, max_idle,
                                 spawn_service,
                                 &lhttp_child_stock_class)),
     mchild_stock(mstock_new(*child_stock)) {}

LhttpStock *
lhttp_stock_new(unsigned limit, unsigned max_idle,
                SpawnService &spawn_service)
{
    return new LhttpStock(limit, max_idle, spawn_service);
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

    return lhttp_stock->hstock.GetNow(*pool,
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
