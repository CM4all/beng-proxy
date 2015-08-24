/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_stock.hxx"
#include "lhttp_quark.hxx"
#include "lhttp_launch.hxx"
#include "lhttp_address.hxx"
#include "hstock.hxx"
#include "mstock.hxx"
#include "stock.hxx"
#include "lease.hxx"
#include "child_stock.hxx"
#include "child_manager.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>

struct LhttpStock {
    StockMap *const hstock;
    struct mstock *const child_stock;

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

struct LhttpConnection {
    StockItem base;

    StockItem *child;

    struct lease_ref lease_ref;

    int fd;
    Event event;

    gcc_pure
    const char *GetName() const {
        return child_stock_item_key(child);
    }

    void EventCallback(evutil_socket_t fd, short events);
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

    stock_del(base);
    pool_commit();
}

/*
 * child_stock class
 *
 */

static int
lhttp_child_stock_socket_type(gcc_unused const char *key, void *info,
                              gcc_unused void *ctx)
{
    const auto &address = *(const LhttpAddress *)info;

    int type = SOCK_STREAM;
    if (!address.blocking)
        type |= SOCK_NONBLOCK;

    return type;
}

static int
lhttp_child_stock_clone_flags(gcc_unused const char *key, void *info, int flags,
                              gcc_unused void *ctx)
{
    auto address = (LhttpAddress *)info;

    return address->options.ns.GetCloneFlags(flags);
}

static int
lhttp_child_stock_run(gcc_unused struct pool *pool, gcc_unused const char *key,
                      void *info, gcc_unused void *ctx)
{
    auto address = (const LhttpAddress *)info;

    address->options.Apply(true);

    lhttp_run(address, 0);
}

static const struct child_stock_class lhttp_child_stock_class = {
    .shutdown_signal = SIGTERM,
    .socket_type = lhttp_child_stock_socket_type,
    .clone_flags = lhttp_child_stock_clone_flags,
    .run = lhttp_child_stock_run,
};

/*
 * stock class
 *
 */

static constexpr LhttpConnection &
ToLhttpConnection(StockItem &item)
{
    return ContainerCast2(item, &LhttpConnection::base);
}

static const constexpr LhttpConnection &
ToLhttpConnection(const StockItem &item)
{
    return ContainerCast2(item, &LhttpConnection::base);
}

static struct pool *
lhttp_stock_pool(gcc_unused void *ctx, struct pool &parent,
                 gcc_unused const char *uri)
{
    return pool_new_linear(&parent, "lhttp_connection", 2048);
}

static void
lhttp_stock_create(void *ctx, StockItem &item,
                   const char *key, void *info,
                   gcc_unused struct pool &caller_pool,
                   gcc_unused struct async_operation_ref &async_ref)
{
    auto lhttp_stock = (LhttpStock *)ctx;
    struct pool *pool = item.pool;
    const auto *address = (const LhttpAddress *)info;
    auto *connection = &ToLhttpConnection(item);

    assert(key != nullptr);
    assert(address != nullptr);
    assert(address->path != nullptr);

    GError *error = nullptr;
    connection->child = mstock_get_now(*lhttp_stock->child_stock, *pool,
                                       key, info, address->concurrency,
                                       connection->lease_ref,
                                       &error);
    if (connection->child == nullptr) {
        g_prefix_error(&error, "failed to launch LHTTP server '%s': ", key);
        stock_item_failed(item, error);
        return;
    }

    connection->fd = child_stock_item_connect(connection->child, &error);

    if (connection->fd < 0) {
        g_prefix_error(&error, "failed to connect to LHTTP server '%s': ",
                       key);
        connection->lease_ref.Release(false);
        stock_item_failed(item, error);
        return;
    }

    connection->event.Set(connection->fd, EV_READ|EV_TIMEOUT,
                          MakeEventCallback(LhttpConnection, EventCallback),
                          connection);

    stock_item_available(connection->base);
}

static bool
lhttp_stock_borrow(void *ctx gcc_unused, StockItem &item)
{
    auto *connection = &ToLhttpConnection(item);

    connection->event.Delete();
    return true;
}

static void
lhttp_stock_release(void *ctx gcc_unused, StockItem &item)
{
    auto *connection = &ToLhttpConnection(item);
    static constexpr struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    connection->event.Add(&tv);
}

static void
lhttp_stock_destroy(gcc_unused void *ctx, StockItem &item)
{
    auto *connection = &ToLhttpConnection(item);

    connection->event.Delete();
    close(connection->fd);

    connection->lease_ref.Release(true);
}

static constexpr StockClass lhttp_stock_class = {
    .item_size = sizeof(LhttpConnection),
    .pool = lhttp_stock_pool,
    .create = lhttp_stock_create,
    .borrow = lhttp_stock_borrow,
    .release = lhttp_stock_release,
    .destroy = lhttp_stock_destroy,
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
    const auto *connection = &ToLhttpConnection(item);

    assert(connection->fd >= 0);

    return connection->fd;
}

FdType
lhttp_stock_item_get_type(gcc_unused const StockItem &item)
{
    return FdType::FD_SOCKET;
}

const char *
lhttp_stock_item_get_name(const StockItem &item)
{
    const auto &connection = ToLhttpConnection(item);

    return connection.GetName();
}

void
lhttp_stock_put(LhttpStock *lhttp_stock, StockItem &item,
                bool destroy)
{
    auto *connection = &ToLhttpConnection(item);

    hstock_put(*lhttp_stock->hstock, child_stock_item_key(connection->child),
               item, destroy);
}
