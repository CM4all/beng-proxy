/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "async.hxx"
#include "failure.hxx"
#include "system/fd_util.h"
#include "system/sigutil.h"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "spawn/Spawn.hxx"
#include "spawn/Prepared.hxx"
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
    const ChildOptions *options;

    PreparedChildProcess child;

    int fds[2];
    sigset_t signals;
};

struct DelegateProcess final : HeapStockItem {
    const char *const uri;

    const pid_t pid;
    const int fd;

    Event event;

    explicit DelegateProcess(CreateStockItem c,
                             const char *_uri,
                             pid_t _pid, int _fd)
        :HeapStockItem(c), uri(_uri), pid(_pid), fd(_fd) {
        event.Set(fd, EV_READ|EV_TIMEOUT,
                  MakeEventCallback(DelegateProcess, EventCallback), this);
    }

    ~DelegateProcess() override {
        if (fd >= 0) {
            event.Delete();
            close(fd);
        }
    }

    void EventCallback(int fd, short event);

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        static constexpr struct timeval tv = {
            .tv_sec = 60,
            .tv_usec = 0,
        };

        event.Add(tv);
        return true;
    }
};

/*
 * libevent callback
 *
 */

inline void
DelegateProcess::EventCallback(gcc_unused int _fd, short events)
{
    assert(_fd == fd);

    if ((events & EV_TIMEOUT) == 0) {
        assert((events & EV_READ) != 0);

        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle delegate process: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle delegate process\n");
    }

    InvokeIdleDisconnect();
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

    info->options->SetupStderr(true);

    Exec(std::move(info->child));
}

/*
 * stock class
 *
 */

static void
delegate_stock_create(gcc_unused void *ctx,
                      gcc_unused struct pool &parent_pool, CreateStockItem c,
                      const char *uri, void *_info,
                      gcc_unused struct pool &caller_pool,
                      gcc_unused struct async_operation_ref &async_ref)
{
    auto *const info = (DelegateArgs *)_info;
    const auto *const options = info->options;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, info->fds) < 0) {
        GError *error = new_error_errno_msg("socketpair() failed: %s");
        c.InvokeCreateError(error);
        return;
    }

    info->child.stdin_fd = info->fds[1];

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
        c.InvokeCreateError(error);
        return;
    }

    leave_signal_section(&info->signals);

    auto *process = new DelegateProcess(c, uri, pid, info->fds[0]);
    process->InvokeCreateSuccess();
}

static constexpr StockClass delegate_stock_class = {
    .create = delegate_stock_create,
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

StockItem *
delegate_stock_get(StockMap *delegate_stock, struct pool *pool,
                   const char *helper,
                   const ChildOptions &options,
                   GError **error_r)
{
    const char *uri = helper;

    char options_buffer[4096];
    *options.MakeId(options_buffer) = 0;
    if (*options_buffer != 0)
        uri = p_strcat(pool, helper, "|", options_buffer, nullptr);

    DelegateArgs args;
    args.options = &options;

    args.child.Append(helper);
    options.CopyTo(args.child, nullptr);

    args.child.refence = options.refence;
    args.child.ns = options.ns;
    args.child.rlimits = options.rlimits;

    return hstock_get_now(*delegate_stock, *pool, uri, &args, error_r);
}

void
delegate_stock_put(StockMap *delegate_stock,
                   StockItem &item, bool destroy)
{
    auto *process = (DelegateProcess *)&item;

    hstock_put(*delegate_stock, process->uri, item, destroy);
}

int
delegate_stock_item_get(StockItem &item)
{
    auto *process = (DelegateProcess *)&item;

    return process->fd;
}
