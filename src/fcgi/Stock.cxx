/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "Quark.hxx"
#include "Launch.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "child_stock.hxx"
#include "child_manager.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/JailConfig.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

struct FcgiStock {
    StockMap *hstock;
    StockMap *child_stock;

    void FadeAll() {
        hstock_fade_all(*hstock);
        hstock_fade_all(*child_stock);
    }
};

struct FcgiChildParams {
    const char *executable_path;

    ConstBuffer<const char *> args;
    ConstBuffer<const char *> env;

    const ChildOptions *options;

    const char *GetStockKey(struct pool &pool) const;
};

struct FcgiConnection final : PoolStockItem {
    JailParams jail_params;

    struct jail_config jail_config;

    StockItem *child = nullptr;

    int fd = -1;
    Event event;

    /**
     * Is this a fresh connection to the FastCGI child process?
     */
    bool fresh;

    /**
     * Shall the FastCGI child process be killed?
     */
    bool kill;

    /**
     * Was the current request aborted by the fcgi_client caller?
     */
    bool aborted;

    explicit FcgiConnection(CreateStockItem c)
        :PoolStockItem(c) {}

    gcc_pure
    const char *GetStockKey() const {
        return child_stock_item_key(child);
    }

    void EventCallback(evutil_socket_t fd, short events);

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override;
    void Release(gcc_unused void *ctx) override;
    void Destroy(void *ctx) override;
};

const char *
FcgiChildParams::GetStockKey(struct pool &pool) const
{
    const char *key = executable_path;

    for (auto i : args)
        key = p_strcat(&pool, key, " ", i, nullptr);

    for (auto i : env)
        key = p_strcat(&pool, key, "$", i, nullptr);

    char options_buffer[4096];
    *options->MakeId(options_buffer) = 0;
    if (*options_buffer != 0)
        key = p_strcat(&pool, key, options_buffer, nullptr);

    return key;
}

/*
 * libevent callback
 *
 */

inline void
FcgiConnection::EventCallback(evutil_socket_t _fd, short events)
{
    assert(_fd == fd);

    if ((events & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(_fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle FastCGI connection '%s': %s\n",
                       GetStockKey(), strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle FastCGI connection '%s'\n",
                       GetStockKey());
    }

    stock_del(*this);
    pool_commit();
}

/*
 * child_stock class
 *
 */

static int
fcgi_child_stock_clone_flags(gcc_unused const char *key, void *info, int flags,
                             gcc_unused void *ctx)
{
    const FcgiChildParams *params =
        (const FcgiChildParams *)info;
    const ChildOptions *const options = params->options;

    return options->ns.GetCloneFlags(flags);
}

static int
fcgi_child_stock_run(gcc_unused struct pool *pool, gcc_unused const char *key,
                     void *info, gcc_unused void *ctx)
{
    const FcgiChildParams *params =
        (const FcgiChildParams *)info;
    const ChildOptions *const options = params->options;

    options->Apply(true);

    fcgi_run(&options->jail, params->executable_path,
             params->args, params->env);
}

static const ChildStockClass fcgi_child_stock_class = {
    .shutdown_signal = SIGUSR1,
    .prepare = nullptr,
    .socket_type = nullptr,
    .clone_flags = fcgi_child_stock_clone_flags,
    .run = fcgi_child_stock_run,
    .free = nullptr,
};

/*
 * stock class
 *
 */

static struct pool *
fcgi_stock_pool(void *ctx gcc_unused, struct pool &parent,
               const char *uri gcc_unused)
{
    return pool_new_linear(&parent, "fcgi_connection", 2048);
}

static void
fcgi_stock_create(void *ctx, CreateStockItem c,
                  const char *key, void *info,
                  gcc_unused struct pool &caller_pool,
                  gcc_unused struct async_operation_ref &async_ref)
{
    FcgiStock *fcgi_stock = (FcgiStock *)ctx;
    FcgiChildParams *params = (FcgiChildParams *)info;

    assert(key != nullptr);
    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    auto *connection = NewFromPool<FcgiConnection>(c.pool, c);

    const ChildOptions *const options = params->options;
    if (options->jail.enabled) {
        connection->jail_params.CopyFrom(c.pool, options->jail);

        if (!jail_config_load(&connection->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", &c.pool)) {
            GError *error = g_error_new(fcgi_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(*connection, error);
            return;
        }
    } else
        connection->jail_params.enabled = false;

    GError *error = nullptr;
    connection->child = hstock_get_now(*fcgi_stock->child_stock, c.pool,
                                       key, params, &error);
    if (connection->child == nullptr) {
        g_prefix_error(&error, "failed to start to FastCGI server '%s': ",
                       key);

        stock_item_failed(*connection, error);
        return;
    }

    connection->fd = child_stock_item_connect(connection->child, &error);
    if (connection->fd < 0) {
        g_prefix_error(&error, "failed to connect to FastCGI server '%s': ",
                       key);

        child_stock_put(fcgi_stock->child_stock, connection->child, true);
        stock_item_failed(*connection, error);
        return;
    }

    connection->fresh = true;
    connection->kill = false;
    connection->aborted = false;

    connection->event.Set(connection->fd, EV_READ|EV_TIMEOUT,
                          MakeEventCallback(FcgiConnection, EventCallback),
                          connection);

    stock_item_available(*connection);
}

bool
FcgiConnection::Borrow(gcc_unused void *ctx)
{
    /* check the connection status before using it, just in case the
       FastCGI server has decided to close the connection before
       fcgi_connection_event_callback() got invoked */
    char buffer;
    ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
    if (nbytes > 0) {
        daemon_log(2, "unexpected data from idle FastCGI connection '%s'\n",
                   GetStockKey());
        return false;
    } else if (nbytes == 0) {
        /* connection closed (not worth a log message) */
        return false;
    } else if (errno != EAGAIN) {
        daemon_log(2, "error on idle FastCGI connection '%s': %s\n",
                   GetStockKey(), strerror(errno));
        return false;
    }

    event.Delete();
    aborted = false;
    return true;
}

void
FcgiConnection::Release(gcc_unused void *ctx)
{
    static constexpr struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    fresh = false;
    event.Add(tv);
}

void
FcgiConnection::Destroy(void *ctx)
{
    FcgiStock *fcgi_stock = (FcgiStock *)ctx;

    if (fd >= 0) {
        event.Delete();
        close(fd);
    }

    if (child != nullptr)
        child_stock_put(fcgi_stock->child_stock, child, kill);

    PoolStockItem::Destroy(ctx);
}

static constexpr StockClass fcgi_stock_class = {
    .pool = fcgi_stock_pool,
    .create = fcgi_stock_create,
};


/*
 * interface
 *
 */

FcgiStock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    auto fcgi_stock = NewFromPool<FcgiStock>(*pool);
    fcgi_stock->child_stock = child_stock_new(pool, limit, max_idle,
                                              &fcgi_child_stock_class);
    fcgi_stock->hstock = hstock_new(*pool, fcgi_stock_class, fcgi_stock,
                                    limit, max_idle);

    return fcgi_stock;
}

void
fcgi_stock_free(FcgiStock *fcgi_stock)
{
    hstock_free(fcgi_stock->hstock);
    hstock_free(fcgi_stock->child_stock);
}

void
fcgi_stock_fade_all(FcgiStock &fs)
{
    fs.FadeAll();
}

StockItem *
fcgi_stock_get(FcgiStock *fcgi_stock, struct pool *pool,
               const ChildOptions &options,
               const char *executable_path,
               ConstBuffer<const char *> args,
               ConstBuffer<const char *> env,
               GError **error_r)
{
    auto params = NewFromPool<FcgiChildParams>(*pool);
    params->executable_path = executable_path;
    params->args = args;
    params->env = env;
    params->options = &options;

    return hstock_get_now(*fcgi_stock->hstock, *pool,
                          params->GetStockKey(*pool), params,
                          error_r);
}

int
fcgi_stock_item_get_domain(gcc_unused const StockItem &item)
{
    return AF_UNIX;
}

int
fcgi_stock_item_get(const StockItem &item)
{
    const auto *connection = (const FcgiConnection *)&item;

    assert(connection->fd >= 0);

    return connection->fd;
}

const char *
fcgi_stock_translate_path(const StockItem &item,
                          const char *path, struct pool *pool)
{
    const auto *connection = (const FcgiConnection *)&item;

    if (!connection->jail_params.enabled)
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = jail_translate_path(&connection->jail_config, path,
                                             connection->jail_params.home_directory,
                                             pool);
    return jailed != nullptr ? jailed : path;
}

void
fcgi_stock_put(FcgiStock *fcgi_stock, StockItem &item,
               bool destroy)
{
    auto *connection = (FcgiConnection *)&item;

    if (connection->fresh && connection->aborted && destroy)
        /* the fcgi_client caller has aborted the request before the
           first response on a fresh connection was received: better
           kill the child process, it may be failing on us
           completely */
        connection->kill = true;

    hstock_put(*fcgi_stock->hstock, connection->GetStockKey(),
               item, destroy);
}

void
fcgi_stock_aborted(StockItem &item)
{
    auto *connection = (FcgiConnection *)&item;

    connection->aborted = true;
}
