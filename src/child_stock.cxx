/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_stock.hxx"
#include "child_socket.hxx"
#include "spawn/ExitListener.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "gerrno.h"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "pool.hxx"

#include <glib.h>

#include <string>

#include <assert.h>
#include <unistd.h>

struct ChildStockItem final : HeapStockItem, ExitListener {
    SpawnService &spawn_service;

    ChildSocket socket;
    int pid = -1;

    bool busy = true;

    ChildStockItem(CreateStockItem c,
                   SpawnService &_spawn_service)
        :HeapStockItem(c),
         spawn_service(_spawn_service) {}

    ~ChildStockItem() override;

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        assert(!busy);
        busy = true;

        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        assert(busy);
        busy = false;

        /* reuse this item only if the child process hasn't exited */
        return pid > 0;
    }

    /* virtual methods from class ExitListener */
    void OnChildProcessExit(int status) override;
};

class ChildStock {
    SpawnService &spawn_service;
    const ChildStockClass &cls;

public:
    explicit ChildStock(SpawnService &_spawn_service,
                        const ChildStockClass &_cls)
        :spawn_service(_spawn_service), cls(_cls) {}

    void Create(CreateStockItem c, void *info);

};

void
ChildStockItem::OnChildProcessExit(gcc_unused int status)
{
    pid = -1;

    if (!busy)
        InvokeIdleDisconnect();
}

/*
 * stock class
 *
 */

inline void
ChildStock::Create(CreateStockItem c, void *info)
{
    GError *error = nullptr;

    auto *item = new ChildStockItem(c, spawn_service);

    int socket_type = cls.socket_type != nullptr
        ? cls.socket_type(info)
        : SOCK_STREAM;

    int fd = item->socket.Create(socket_type, &error);
    if (fd < 0) {
        item->InvokeCreateError(error);
        return;
    }

    PreparedChildProcess p;
    if (!cls.prepare(info, fd, p, &error)) {
        item->InvokeCreateError(error);
        return;
    }

    int pid = item->pid =
        spawn_service.SpawnChildProcess(item->GetStockName(), std::move(p),
                                        item, &error);
    if (pid < 0) {
        item->InvokeCreateError(error);
        return;
    }

    item->InvokeCreateSuccess();
}

static void
child_stock_create(void *stock_ctx,
                   CreateStockItem c,
                   void *info,
                   gcc_unused struct pool &caller_pool,
                   gcc_unused struct async_operation_ref &async_ref)
{
    auto &stock = *(ChildStock *)stock_ctx;

    stock.Create(c, info);
}

ChildStockItem::~ChildStockItem()
{
    if (pid >= 0)
        spawn_service.KillChildProcess(pid);

    if (socket.IsDefined())
        socket.Unlink();
}

static constexpr StockClass child_stock_class = {
    .create = child_stock_create,
};


/*
 * interface
 *
 */

StockMap *
child_stock_new(unsigned limit, unsigned max_idle,
                SpawnService &spawn_service,
                const ChildStockClass *cls)
{
    assert(cls != nullptr);
    assert(cls->prepare != nullptr);

    auto *s = new ChildStock(spawn_service, *cls);
    return new StockMap(child_stock_class, s, limit, max_idle);
}

void
child_stock_free(StockMap *stock)
{
    auto *s = (ChildStock *)stock->class_ctx;
    delete stock;
    delete s;
}

int
child_stock_item_connect(StockItem *_item, GError **error_r)
{
    auto *item = (ChildStockItem *)_item;

    int fd = item->socket.Connect(error_r);
    if (fd < 0)
        /* if the connection fails, abandon the child process, don't
           try again - it will never work! */
        item->fade = true;

    return fd;
}
