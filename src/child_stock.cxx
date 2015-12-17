/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_stock.hxx"
#include "child_socket.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "child_manager.hxx"
#include "gerrno.h"
#include "system/sigutil.h"
#include "pool.hxx"

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <sched.h>

struct ChildStockItem final : PoolStockItem {
    const char *key;

    const ChildStockClass *cls;
    void *cls_ctx = nullptr;

    ChildSocket socket;
    pid_t pid = -1;

    bool busy;

    ChildStockItem(struct pool &_pool, CreateStockItem c)
        :PoolStockItem(_pool, c) {}

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
};

static void
child_stock_child_callback(int status gcc_unused, void *ctx)
{
    auto *item = (ChildStockItem *)ctx;

    item->pid = -1;

    if (!item->busy)
        item->InvokeIdleDisconnect();
}

struct ChildStockArgs {
    struct pool *pool;
    const char *key;
    void *info;
    const ChildStockClass *cls;
    void *cls_ctx;
    int fd;
    sigset_t *signals;
};

gcc_noreturn
static int
child_stock_fn(void *ctx)
{
    const auto *args = (const ChildStockArgs *)ctx;
    const int fd = args->fd;

    install_default_signal_handlers();
    leave_signal_section(args->signals);

    dup2(fd, 0);
    close(fd);

    int result = args->cls->run(args->pool, args->key, args->info,
                                args->cls_ctx);
    _exit(result);
}

static pid_t
child_stock_start(struct pool *pool, const char *key, void *info,
                  int clone_flags,
                  const ChildStockClass *cls, void *ctx,
                  int fd, GError **error_r)
{
    /* avoid race condition due to libevent signal handler in child
       process */
    sigset_t signals;
    enter_signal_section(&signals);

    ChildStockArgs args = {
        pool, key, info,
        cls, ctx,
        fd,
        &signals,
    };

    char stack[8192];

    int pid = clone(child_stock_fn, stack + sizeof(stack),
                    clone_flags, &args);
    if (pid < 0) {
        set_error_errno_msg(error_r, "clone() failed");
        leave_signal_section(&signals);
        return -1;
    }

    leave_signal_section(&signals);

    close(fd);

    return pid;
}

/*
 * stock class
 *
 */

static struct pool *
child_stock_pool(void *ctx gcc_unused, struct pool &parent,
                 const char *uri gcc_unused)
{
    return pool_new_linear(&parent, "child_stock_child", 2048);
}

static void
child_stock_create(void *stock_ctx, struct pool &pool, CreateStockItem c,
                   const char *key, void *info,
                   gcc_unused struct pool &caller_pool,
                   gcc_unused struct async_operation_ref &async_ref)
{
    const auto *cls = (const ChildStockClass *)stock_ctx;

    auto *item = NewFromPool<ChildStockItem>(pool, pool, c);

    item->key = key = p_strdup(&pool, key);
    item->cls = cls;

    GError *error = nullptr;
    void *cls_ctx = nullptr;
    if (cls->prepare != nullptr) {
        cls_ctx = cls->prepare(&pool, key, info, &error);
        if (cls_ctx == nullptr) {
            item->InvokeCreateError(error);
            return;
        }
    }

    item->cls_ctx = cls_ctx;

    int socket_type = cls->socket_type != nullptr
        ? cls->socket_type(key, info, cls_ctx)
        : SOCK_STREAM;

    int fd = item->socket.Create(socket_type, &error);
    if (fd < 0) {
        if (cls_ctx != nullptr)
            cls->free(cls_ctx);
        item->InvokeCreateError(error);
        return;
    }

    int clone_flags = SIGCHLD;
    if (cls->clone_flags != nullptr)
        clone_flags = cls->clone_flags(key, info, clone_flags, cls_ctx);

    pid_t pid = item->pid = child_stock_start(&pool, key, info, clone_flags,
                                              cls, cls_ctx, fd, &error);
    if (pid < 0) {
        if (cls_ctx != nullptr)
            cls->free(cls_ctx);
        item->InvokeCreateError(error);
        return;
    }

    child_register(pid, key, child_stock_child_callback, item);

    item->busy = true;
    item->InvokeCreateSuccess();
}

ChildStockItem::~ChildStockItem()
{
    if (pid >= 0)
        child_kill_signal(pid, cls->shutdown_signal);

    if (socket.IsDefined())
        socket.Unlink();

    if (cls_ctx != nullptr)
        cls->free(cls_ctx);
}

static constexpr StockClass child_stock_class = {
    .pool = child_stock_pool,
    .create = child_stock_create,
};


/*
 * interface
 *
 */

StockMap *
child_stock_new(struct pool *pool, unsigned limit, unsigned max_idle,
                const ChildStockClass *cls)
{
    assert(cls != nullptr);
    assert((cls->prepare == nullptr) == (cls->free == nullptr));
    assert(cls->shutdown_signal != 0);
    assert(cls->run != nullptr);

    union {
        const ChildStockClass *in;
        void *out;
    } u = { .in = cls };

    return hstock_new(*pool, child_stock_class, u.out, limit, max_idle);
}

const char *
child_stock_item_key(const StockItem *_item)
{
    const auto *item = (const ChildStockItem *)_item;

    return item->key;
}

int
child_stock_item_connect(const StockItem *_item, GError **error_r)
{
    const auto *item = (const ChildStockItem *)_item;

    return item->socket.Connect(error_r);
}

void
child_stock_put(StockMap *hstock, StockItem *_item,
                bool destroy)
{
    auto *item = (ChildStockItem *)_item;

    hstock_put(*hstock, item->key, *item, destroy);
}
