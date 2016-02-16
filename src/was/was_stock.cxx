/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_stock.hxx"
#include "was_quark.h"
#include "was_launch.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "async.hxx"
#include "net/ConnectSocket.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "pool.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <glib.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <stdlib.h>

struct WasChildParams {
    const char *executable_path;

    ConstBuffer<const char *> args;

    const ChildOptions &options;

    WasChildParams(const char *_executable_path,
                   ConstBuffer<const char *> _args,
                   const ChildOptions &_options)
        :executable_path(_executable_path), args(_args),
         options(_options) {}

    const char *GetStockKey(struct pool &pool) const;
};

struct WasChild final : HeapStockItem, ExitListener {
    SpawnService &spawn_service;

    WasProcess process;
    Event event;

    explicit WasChild(CreateStockItem c, SpawnService &_spawn_service)
        :HeapStockItem(c), spawn_service(_spawn_service) {}

    ~WasChild() override;

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

    /* virtual methods from class ExitListener */
    void OnChildProcessExit(gcc_unused int status) override {
        process.pid = -1;
    }
};

const char *
WasChildParams::GetStockKey(struct pool &pool) const
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
WasChild::EventCallback(evutil_socket_t fd, short events)
{
    assert(fd == process.control_fd);

    if ((events & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle WAS control connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle WAS control connection\n");
    }

    InvokeIdleDisconnect();
    pool_commit();
}

/*
 * stock class
 *
 */

static void
was_stock_create(gcc_unused void *ctx,
                 CreateStockItem c,
                 void *info,
                 gcc_unused struct pool &caller_pool,
                 gcc_unused struct async_operation_ref &async_ref)
{
    auto &spawn_service = *(SpawnService *)ctx;
    WasChildParams *params = (WasChildParams *)info;

    auto *child = new WasChild(c, spawn_service);

    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    GError *error = nullptr;
    if (!was_launch(spawn_service, &child->process,
                    child->GetStockName(),
                    params->executable_path,
                    params->args,
                    params->options,
                    child,
                    &error)) {
        child->InvokeCreateError(error);
        return;
    }

    child->event.Set(child->process.control_fd, EV_READ|EV_TIMEOUT,
                     MakeEventCallback(WasChild, EventCallback), child);

    child->InvokeCreateSuccess();
}

WasChild::~WasChild()
{
    if (process.pid >= 0)
        spawn_service.KillChildProcess(process.pid);

    if (process.control_fd >= 0)
        event.Delete();

    process.Close();
}

static constexpr StockClass was_stock_class = {
    .create = was_stock_create,
};


/*
 * interface
 *
 */

StockMap *
was_stock_new(unsigned limit, unsigned max_idle,
              SpawnService &spawn_service)
{
    return hstock_new(was_stock_class, &spawn_service, limit, max_idle);
}

void
was_stock_get(StockMap *hstock, struct pool *pool,
              const ChildOptions &options,
              const char *executable_path,
              ConstBuffer<const char *> args,
              StockGetHandler &handler,
              struct async_operation_ref &async_ref)
{
    auto params = NewFromPool<WasChildParams>(*pool, executable_path, args,
                                              options);

    hstock_get(*hstock, *pool, params->GetStockKey(*pool), params,
               handler, async_ref);
}

const WasProcess &
was_stock_item_get(const StockItem &item)
{
    auto *child = (const WasChild *)&item;

    return child->process;
}
