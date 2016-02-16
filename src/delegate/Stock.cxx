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
#include <sys/un.h>
#include <sys/socket.h>

struct DelegateArgs {
    const char *executable_path;

    const ChildOptions *options;
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
    auto &info = *(DelegateArgs *)_info;

    PreparedChildProcess p;
    p.Append(info.executable_path);

    GError *error = nullptr;
    if (!info.options->CopyTo(p, true, nullptr, &error)) {
        c.InvokeCreateError(error);
        return;
    }

    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        error = new_error_errno_msg("socketpair() failed: %s");
        c.InvokeCreateError(error);
        return;
    }

    p.stdin_fd = fds[1];

    pid_t pid = SpawnChildProcess(std::move(p));
    if (pid < 0) {
        error = new_error_errno_msg2(-pid, "clone() failed");
        close(fds[0]);
        c.InvokeCreateError(error);
        return;
    }

    auto *process = new DelegateProcess(c, uri, pid, fds[0]);
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
    args.executable_path = helper;
    args.options = &options;

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
