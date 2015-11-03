/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "async.hxx"
#include "failure.hxx"
#include "system/fd_util.h"
#include "system/sigutil.h"
#include "pevent.hxx"
#include "spawn/exec.hxx"
#include "spawn/ChildOptions.hxx"
#include "gerrno.h"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sched.h>
#include <sys/un.h>
#include <sys/socket.h>

struct DelegateArgs {
    const char *helper;

    const ChildOptions *options;

    int fds[2];
    sigset_t signals;
};

struct DelegateProcess {
    StockItem stock_item;

    const char *uri;

    pid_t pid;
    int fd;

    struct event event;
};

/*
 * libevent callback
 *
 */

static void
delegate_stock_event(int fd, short event, void *ctx)
{
    auto *process = (DelegateProcess *)ctx;

    assert(fd == process->fd);

    p_event_consumed(&process->event, process->stock_item.pool);

    if ((event & EV_TIMEOUT) == 0) {
        assert((event & EV_READ) != 0);

        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle delegate process: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle delegate process\n");
    }

    stock_del(process->stock_item);
    pool_commit();
}

/*
 * clone() function
 *
 */

static int
delegate_stock_fn(void *ctx)
{
    auto *info = (DelegateArgs *)ctx;

    install_default_signal_handlers();
    leave_signal_section(&info->signals);

    info->options->Apply(true);

    dup2(info->fds[1], STDIN_FILENO);
    close(info->fds[0]);
    close(info->fds[1]);

    Exec e;
    info->options->jail.InsertWrapper(e, nullptr);
    e.Append(info->helper);
    e.DoExec();
}

/*
 * stock class
 *
 */

static constexpr DelegateProcess &
ToDelegateProcess(StockItem &item)
{
    return ContainerCast2(item, &DelegateProcess::stock_item);
}

static struct pool *
delegate_stock_pool(void *ctx gcc_unused, struct pool &parent,
                    const char *uri gcc_unused)
{
    return pool_new_linear(&parent, "delegate_stock", 512);
}

static void
delegate_stock_create(gcc_unused void *ctx, StockItem &item,
                      const char *uri, void *_info,
                      gcc_unused struct pool &caller_pool,
                      gcc_unused struct async_operation_ref &async_ref)
{
    auto *process = &ToDelegateProcess(item);
    auto *const info = (DelegateArgs *)_info;
    const auto *const options = info->options;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, info->fds) < 0) {
        GError *error = new_error_errno_msg("socketpair() failed: %s");
        stock_item_failed(item, error);
        return;
    }

    int clone_flags = SIGCHLD;
    clone_flags = options->ns.GetCloneFlags(clone_flags);

    /* avoid race condition due to libevent signal handler in child
       process */
    enter_signal_section(&info->signals);

    char stack[8192];
    long pid = clone(delegate_stock_fn, stack + sizeof(stack),
                     clone_flags, info);
    if (pid < 0) {
        GError *error = new_error_errno_msg("clone() failed");
        leave_signal_section(&info->signals);
        close(info->fds[0]);
        close(info->fds[1]);
        stock_item_failed(item, error);
        return;
    }

    leave_signal_section(&info->signals);

    close(info->fds[1]);

    process->uri = uri;
    process->pid = pid;
    process->fd = info->fds[0];

    event_set(&process->event, process->fd, EV_READ|EV_TIMEOUT,
              delegate_stock_event, process);

    stock_item_available(process->stock_item);
}

static bool
delegate_stock_borrow(gcc_unused void *ctx, StockItem &item)
{
    auto *process = &ToDelegateProcess(item);

    p_event_del(&process->event, process->stock_item.pool);
    return true;
}

static void
delegate_stock_release(gcc_unused void *ctx, StockItem &item)
{
    auto *process = &ToDelegateProcess(item);
    static const struct timeval tv = {
        .tv_sec = 60,
        .tv_usec = 0,
    };

    p_event_add(&process->event, &tv,
                process->stock_item.pool, "delegate_process");
}

static void
delegate_stock_destroy(gcc_unused void *ctx, StockItem &item)
{
    auto *process = &ToDelegateProcess(item);

    p_event_del(&process->event, process->stock_item.pool);
    close(process->fd);
}

static constexpr StockClass delegate_stock_class = {
    .item_size = sizeof(DelegateProcess),
    .pool = delegate_stock_pool,
    .create = delegate_stock_create,
    .borrow = delegate_stock_borrow,
    .release = delegate_stock_release,
    .destroy = delegate_stock_destroy,
};


/*
 * interface
 *
 */

StockMap *
delegate_stock_new(struct pool *pool)
{
    return hstock_new(*pool, delegate_stock_class, nullptr, 0, 16);
}

void
delegate_stock_get(StockMap *delegate_stock, struct pool *pool,
                   const char *helper,
                   const ChildOptions &options,
                   StockGetHandler &handler,
                   struct async_operation_ref &async_ref)
{
    const char *uri = helper;

    char options_buffer[4096];
    *options.MakeId(options_buffer) = 0;
    if (*options_buffer != 0)
        uri = p_strcat(pool, helper, "|", options_buffer, nullptr);

    auto info = NewFromPool<DelegateArgs>(*pool);
    info->helper = helper;
    info->options = &options;

    hstock_get(*delegate_stock, *pool, uri, info, handler, async_ref);
}

void
delegate_stock_put(StockMap *delegate_stock,
                   StockItem &item, bool destroy)
{
    auto *process = &ToDelegateProcess(item);

    hstock_put(*delegate_stock, process->uri, item, destroy);
}

int
delegate_stock_item_get(StockItem &item)
{
    auto *process = &ToDelegateProcess(item);

    return process->fd;
}
