/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "Quark.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "child_stock.hxx"
#include "child_manager.hxx"
#include "spawn/Prepared.hxx"
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
#include <fcntl.h>
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

    const ChildOptions &options;

    FcgiChildParams(const char *_executable_path,
                    ConstBuffer<const char *> _args,
                    const ChildOptions &_options)
        :executable_path(_executable_path), args(_args),
         options(_options) {}

    const char *GetStockKey(struct pool &pool) const;
};

struct FcgiConnection final : PoolStockItem {
    const char *jail_home_directory = nullptr;

    JailConfig jail_config;

    StockItem *child = nullptr;

    int fd = -1;
    Event event;

    /**
     * Is this a fresh connection to the FastCGI child process?
     */
    bool fresh = true;

    /**
     * Shall the FastCGI child process be killed?
     */
    bool kill = false;

    /**
     * Was the current request aborted by the fcgi_client caller?
     */
    bool aborted = false;

    explicit FcgiConnection(struct pool &_pool, CreateStockItem c)
        :PoolStockItem(_pool, c) {}

    void EventCallback(evutil_socket_t fd, short events);

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override;
    bool Release(gcc_unused void *ctx) override;
    void Destroy(void *ctx) override;
};

const char *
FcgiChildParams::GetStockKey(struct pool &pool) const
{
    const char *key = executable_path;

    for (auto i : args)
        key = p_strcat(&pool, key, " ", i, nullptr);

    for (auto i : options.env)
        key = p_strcat(&pool, key, "$", i, nullptr);

    char options_buffer[4096];
    *options.MakeId(options_buffer) = 0;
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
                       GetStockName(), strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle FastCGI connection '%s'\n",
                       GetStockName());
    }

    InvokeIdleDisconnect();
    pool_commit();
}

/*
 * child_stock class
 *
 */

static bool
fcgi_child_stock_prepare(void *info, int fd,
                         PreparedChildProcess &p, GError **error_r)
{
    const auto &params = *(const FcgiChildParams *)info;
    const ChildOptions &options = params.options;

    p.stdin_fd = fd;

    /* the FastCGI protocol defines a channel for stderr, so we could
       close its "real" stderr here, but many FastCGI applications
       don't use the FastCGI protocol to send error messages, so we
       just keep it open */

    int null_fd = open("/dev/null", O_WRONLY|O_CLOEXEC|O_NOCTTY);
    if (null_fd >= 0)
        p.stdout_fd = null_fd;

    p.Append(params.executable_path);
    for (auto i : params.args)
        p.Append(i);

    return options.CopyTo(p, true, nullptr, error_r);
}

static const ChildStockClass fcgi_child_stock_class = {
    .shutdown_signal = SIGUSR1,
    .socket_type = nullptr,
    .prepare = fcgi_child_stock_prepare,
};

/*
 * stock class
 *
 */

static void
fcgi_stock_create(void *ctx, struct pool &parent_pool, CreateStockItem c,
                  void *info,
                  struct pool &caller_pool,
                  gcc_unused struct async_operation_ref &async_ref)
{
    FcgiStock *fcgi_stock = (FcgiStock *)ctx;
    FcgiChildParams *params = (FcgiChildParams *)info;

    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    auto &pool = *pool_new_linear(&parent_pool, "fcgi_connection", 2048);
    auto *connection = NewFromPool<FcgiConnection>(pool, pool, c);

    const ChildOptions &options = params->options;
    if (options.jail.enabled) {
        connection->jail_home_directory =
            p_strdup(pool, options.jail.home_directory);

        if (!connection->jail_config.Load("/etc/cm4all/jailcgi/jail.conf",
                                          &pool)) {
            GError *error = g_error_new(fcgi_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            connection->InvokeCreateError(error);
            return;
        }
    }

    const char *key = c.GetStockName();

    GError *error = nullptr;
    connection->child = hstock_get_now(*fcgi_stock->child_stock, caller_pool,
                                       key, params, &error);
    if (connection->child == nullptr) {
        g_prefix_error(&error, "failed to start to FastCGI server '%s': ",
                       key);
        connection->InvokeCreateError(error);
        return;
    }

    connection->fd = child_stock_item_connect(connection->child, &error);
    if (connection->fd < 0) {
        g_prefix_error(&error, "failed to connect to FastCGI server '%s': ",
                       key);

        connection->kill = true;
        connection->InvokeCreateError(error);
        return;
    }

    connection->event.Set(connection->fd, EV_READ|EV_TIMEOUT,
                          MakeEventCallback(FcgiConnection, EventCallback),
                          connection);

    connection->InvokeCreateSuccess();
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
                   GetStockName());
        return false;
    } else if (nbytes == 0) {
        /* connection closed (not worth a log message) */
        return false;
    } else if (errno != EAGAIN) {
        daemon_log(2, "error on idle FastCGI connection '%s': %s\n",
                   GetStockName(), strerror(errno));
        return false;
    }

    event.Delete();
    aborted = false;
    return true;
}

bool
FcgiConnection::Release(gcc_unused void *ctx)
{
    static constexpr struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    fresh = false;
    event.Add(tv);
    return true;
}

void
FcgiConnection::Destroy(void *ctx)
{
    if (fd >= 0) {
        event.Delete();
        close(fd);
    }

    if (fresh && aborted)
        /* the fcgi_client caller has aborted the request before the
           first response on a fresh connection was received: better
           kill the child process, it may be failing on us
           completely */
        kill = true;

    if (child != nullptr)
        child->Put(kill);

    PoolStockItem::Destroy(ctx);
}

static constexpr StockClass fcgi_stock_class = {
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
               GError **error_r)
{
    auto params = NewFromPool<FcgiChildParams>(*pool, executable_path,
                                               args, options);

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

    if (connection->jail_home_directory == nullptr)
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = connection->jail_config.TranslatePath(path,
                                                               connection->jail_home_directory,
                                                               pool);
    return jailed != nullptr ? jailed : path;
}

void
fcgi_stock_aborted(StockItem &item)
{
    auto *connection = (FcgiConnection *)&item;

    connection->aborted = true;
}
