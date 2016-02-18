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
#include "child_manager.hxx"
#include "gerrno.h"
#include "spawn/Local.hxx"
#include "spawn/Prepared.hxx"
#include "pool.hxx"

#include <glib.h>

#include <string>

#include <assert.h>
#include <unistd.h>

struct ChildStockItem final : HeapStockItem, ExitListener {
    const int shutdown_signal;

    ChildSocket socket;
    pid_t pid = -1;

    bool busy = true;

    ChildStockItem(CreateStockItem c,
                   const ChildStockClass &_cls)
        :HeapStockItem(c),
         shutdown_signal(_cls.shutdown_signal) {}

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

static void
child_stock_create(void *stock_ctx,
                   CreateStockItem c,
                   void *info,
                   gcc_unused struct pool &caller_pool,
                   gcc_unused struct async_operation_ref &async_ref)
{
    const auto *cls = (const ChildStockClass *)stock_ctx;

    GError *error = nullptr;

    auto *item = new ChildStockItem(c, *cls);

    int socket_type = cls->socket_type != nullptr
        ? cls->socket_type(info)
        : SOCK_STREAM;

    int fd = item->socket.Create(socket_type, &error);
    if (fd < 0) {
        item->InvokeCreateError(error);
        return;
    }

    PreparedChildProcess p;
    if (!cls->prepare(info, fd, p, &error)) {
        item->InvokeCreateError(error);
        return;
    }

    pid_t pid = item->pid = SpawnChildProcess(std::move(p));
    if (pid < 0) {
        item->InvokeCreateError(new_error_errno_msg2(-pid, "fork() failed"));
        return;
    }

    child_register(pid, item->GetStockName(), item);

    item->InvokeCreateSuccess();
}

ChildStockItem::~ChildStockItem()
{
    if (pid >= 0)
        child_kill_signal(pid, shutdown_signal);

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
                const ChildStockClass *cls)
{
    assert(cls != nullptr);
    assert(cls->shutdown_signal != 0);
    assert(cls->prepare != nullptr);

    union {
        const ChildStockClass *in;
        void *out;
    } u = { .in = cls };

    return hstock_new(child_stock_class, u.out, limit, max_idle);
}

int
child_stock_item_connect(const StockItem *_item, GError **error_r)
{
    const auto *item = (const ChildStockItem *)_item;

    return item->socket.Connect(error_r);
}
