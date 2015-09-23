/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_stock.hxx"
#include "child_socket.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "child_manager.hxx"
#include "gerrno.h"
#include "system/sigutil.h"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <sched.h>

struct child_stock_item {
    StockItem base;

    const char *key;

    const struct child_stock_class *cls;
    void *cls_ctx;

    struct child_socket socket;
    pid_t pid;

    bool busy;
};

static void
child_stock_child_callback(int status gcc_unused, void *ctx)
{
    auto *item = (struct child_stock_item *)ctx;

    item->pid = -1;

    if (!item->busy)
        stock_del(item->base);
}

struct child_stock_args {
    struct pool *pool;
    const char *key;
    void *info;
    const struct child_stock_class *cls;
    void *cls_ctx;
    int fd;
    sigset_t *signals;
};

gcc_noreturn
static int
child_stock_fn(void *ctx)
{
    const struct child_stock_args *args = (const struct child_stock_args *)ctx;
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
                  const struct child_stock_class *cls, void *ctx,
                  int fd, GError **error_r)
{
    /* avoid race condition due to libevent signal handler in child
       process */
    sigset_t signals;
    enter_signal_section(&signals);

    struct child_stock_args args = {
        pool, key, info,
        cls, ctx,
        fd,
        &signals,
    };

    char stack[8192];

    long pid = clone(child_stock_fn, stack + sizeof(stack),
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

static constexpr struct child_stock_item &
ToChildStockItem(StockItem &item)
{
    return ContainerCast2(item, &child_stock_item::base);
}

static constexpr const struct child_stock_item &
ToChildStockItem(const StockItem &item)
{
    return ContainerCast2(item, &child_stock_item::base);
}

static struct pool *
child_stock_pool(void *ctx gcc_unused, struct pool &parent,
                 const char *uri gcc_unused)
{
    return pool_new_linear(&parent, "child_stock_child", 2048);
}

static void
child_stock_create(void *stock_ctx, StockItem &_item,
                   const char *key, void *info,
                   gcc_unused struct pool &caller_pool,
                   gcc_unused struct async_operation_ref &async_ref)
{
    const struct child_stock_class *cls =
        (const struct child_stock_class *)stock_ctx;
    struct pool *pool = _item.pool;
    auto *item = &ToChildStockItem(_item);

    item->key = key = p_strdup(pool, key);
    item->cls = cls;

    GError *error = nullptr;
    void *cls_ctx = nullptr;
    if (cls->prepare != nullptr) {
        cls_ctx = cls->prepare(pool, key, info, &error);
        if (cls_ctx == nullptr) {
            stock_item_failed(_item, error);
            return;
        }
    }

    item->cls_ctx = cls_ctx;

    int socket_type = cls->socket_type != nullptr
        ? cls->socket_type(key, info, cls_ctx)
        : SOCK_STREAM;

    int fd = child_socket_create(&item->socket, socket_type, &error);
    if (fd < 0) {
        if (cls_ctx != nullptr)
            cls->free(cls_ctx);
        stock_item_failed(_item, error);
        return;
    }

    int clone_flags = SIGCHLD;
    if (cls->clone_flags != nullptr)
        clone_flags = cls->clone_flags(key, info, clone_flags, cls_ctx);

    pid_t pid = item->pid = child_stock_start(pool, key, info, clone_flags,
                                              cls, cls_ctx, fd, &error);
    if (pid < 0) {
        if (cls_ctx != nullptr)
            cls->free(cls_ctx);
        stock_item_failed(_item, error);
        return;
    }

    child_register(pid, key, child_stock_child_callback, item);

    item->busy = true;
    stock_item_available(item->base);
}

static bool
child_stock_borrow(gcc_unused void *ctx, StockItem &_item)
{
    auto *item = &ToChildStockItem(_item);

    assert(!item->busy);
    item->busy = true;

    return true;
}

static void
child_stock_release(gcc_unused void *ctx, StockItem &_item)
{
    auto *item = &ToChildStockItem(_item);

    assert(item->busy);
    item->busy = false;

    if (item->pid < 0)
        /* the child process has exited; now that the item has been
           released, we can remove it entirely */
        stock_del(_item);
}

static void
child_stock_destroy(void *ctx gcc_unused, StockItem &_item)
{
    auto *item = &ToChildStockItem(_item);

    if (item->pid >= 0)
        child_kill_signal(item->pid, item->cls->shutdown_signal);

    child_socket_unlink(&item->socket);

    if (item->cls_ctx != nullptr)
        item->cls->free(item->cls_ctx);
}

static constexpr StockClass child_stock_class = {
    .item_size = sizeof(struct child_stock_item),
    .pool = child_stock_pool,
    .create = child_stock_create,
    .borrow = child_stock_borrow,
    .release = child_stock_release,
    .destroy = child_stock_destroy,
};


/*
 * interface
 *
 */

StockMap *
child_stock_new(struct pool *pool, unsigned limit, unsigned max_idle,
                const struct child_stock_class *cls)
{
    assert(cls != nullptr);
    assert((cls->prepare == nullptr) == (cls->free == nullptr));
    assert(cls->shutdown_signal != 0);
    assert(cls->run != nullptr);

    union {
        const struct child_stock_class *in;
        void *out;
    } u = { .in = cls };

    return hstock_new(*pool, child_stock_class, u.out, limit, max_idle);
}

const char *
child_stock_item_key(const StockItem *_item)
{
    const auto *item = &ToChildStockItem(*_item);

    return item->key;
}

int
child_stock_item_connect(const StockItem *_item, GError **error_r)
{
    const auto *item = &ToChildStockItem(*_item);

    return child_socket_connect(&item->socket, error_r);
}

void
child_stock_put(StockMap *hstock, StockItem *_item,
                bool destroy)
{
    auto *item = &ToChildStockItem(*_item);

    hstock_put(*hstock, item->key, item->base, destroy);
}
