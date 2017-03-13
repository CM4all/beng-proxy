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
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/JailParams.hxx"
#include "spawn/JailConfig.hxx"
#include "GException.hxx"
#include "pool.hxx"
#include "event/SocketEvent.hxx"
#include "event/Duration.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <string>

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

struct FcgiStock {
    StockMap hstock;
    StockMap *child_stock;

    FcgiStock(unsigned limit, unsigned max_idle,
              EventLoop &event_loop, SpawnService &spawn_service);

    ~FcgiStock() {
        /* this one must be cleared before #child_stock; FadeAll()
           calls ClearIdle(), so this method is the best match for
           what we want to do (though a kludge) */
        hstock.FadeAll();

        child_stock_free(child_stock);
    }

    EventLoop &GetEventLoop() {
        return hstock.GetEventLoop();
    }

    void FadeAll() {
        hstock.FadeAll();
        child_stock->FadeAll();
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

struct FcgiConnection final : HeapStockItem {
    std::string jail_home_directory;

    JailConfig jail_config;

    StockItem *child = nullptr;

    int fd = -1;
    SocketEvent event;

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

    explicit FcgiConnection(EventLoop &event_loop, CreateStockItem c)
        :HeapStockItem(c),
         event(event_loop, BIND_THIS_METHOD(OnSocketEvent)) {}

    ~FcgiConnection() override;

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override;
    bool Release(gcc_unused void *ctx) override;

private:
    void OnSocketEvent(unsigned events);
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

void
FcgiConnection::OnSocketEvent(unsigned events)
{
    if ((events & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
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
fcgi_child_stock_prepare(void *info, UniqueFileDescriptor &&fd,
                         PreparedChildProcess &p, GError **error_r)
{
    const auto &params = *(const FcgiChildParams *)info;
    const ChildOptions &options = params.options;

    p.SetStdin(std::move(fd));

    /* the FastCGI protocol defines a channel for stderr, so we could
       close its "real" stderr here, but many FastCGI applications
       don't use the FastCGI protocol to send error messages, so we
       just keep it open */

    UniqueFileDescriptor null_fd;
    if (null_fd.Open("/dev/null", O_WRONLY))
        p.SetStdout(std::move(null_fd));

    p.Append(params.executable_path);
    for (auto i : params.args)
        p.Append(i);

    try {
        options.CopyTo(p, true, nullptr);
    } catch (const std::runtime_error &e) {
        SetGError(error_r, e);
        return false;
    }

    return true;
}

static const ChildStockClass fcgi_child_stock_class = {
    .socket_type = nullptr,
    .prepare = fcgi_child_stock_prepare,
};

/*
 * stock class
 *
 */

static void
fcgi_stock_create(void *ctx, CreateStockItem c, void *info,
                  struct pool &caller_pool,
                  gcc_unused CancellablePointer &cancel_ptr)
{
    FcgiStock *fcgi_stock = (FcgiStock *)ctx;
    FcgiChildParams *params = (FcgiChildParams *)info;

    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    auto *connection = new FcgiConnection(fcgi_stock->GetEventLoop(), c);

    const ChildOptions &options = params->options;
    if (options.jail != nullptr && options.jail->enabled) {
        connection->jail_home_directory = options.jail->home_directory;

        if (!connection->jail_config.Load("/etc/cm4all/jailcgi/jail.conf")) {
            GError *error = g_error_new(fcgi_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            connection->InvokeCreateError(error);
            return;
        }
    }

    const char *key = c.GetStockName();

    GError *error = nullptr;
    connection->child = fcgi_stock->child_stock->GetNow(caller_pool,
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

    connection->event.Set(connection->fd, EV_READ);

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
    fresh = false;
    event.Add(EventDuration<300>::value);
    return true;
}

FcgiConnection::~FcgiConnection()
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
}

static constexpr StockClass fcgi_stock_class = {
    .create = fcgi_stock_create,
};


/*
 * interface
 *
 */

inline
FcgiStock::FcgiStock(unsigned limit, unsigned max_idle,
                     EventLoop &event_loop, SpawnService &spawn_service)
    :hstock(event_loop, fcgi_stock_class, this, limit, max_idle),
     child_stock(child_stock_new(limit, max_idle,
                                 event_loop, spawn_service,
                                 &fcgi_child_stock_class)) {}

FcgiStock *
fcgi_stock_new(unsigned limit, unsigned max_idle,
               EventLoop &event_loop, SpawnService &spawn_service)
{
    return new FcgiStock(limit, max_idle, event_loop, spawn_service);
}

void
fcgi_stock_free(FcgiStock *fcgi_stock)
{
    delete fcgi_stock;
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

    return fcgi_stock->hstock.GetNow(*pool,
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

    if (connection->jail_home_directory.empty())
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = connection->jail_config.TranslatePath(path,
                                                               connection->jail_home_directory.c_str(),
                                                               pool);
    return jailed != nullptr ? jailed : path;
}

void
fcgi_stock_aborted(StockItem &item)
{
    auto *connection = (FcgiConnection *)&item;

    connection->aborted = true;
}
