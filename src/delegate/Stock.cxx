/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "failure.hxx"
#include "system/fd_util.h"
#include "event/SocketEvent.hxx"
#include "event/Duration.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "gerrno.h"
#include "GException.hxx"
#include "AllocatorPtr.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct DelegateArgs {
    const char *executable_path;

    const ChildOptions &options;

    DelegateArgs(const char *_executable_path,
                 const ChildOptions &_options)
        :executable_path(_executable_path), options(_options) {}

    const char *GetStockKey(struct pool &pool) const {
        const char *key = executable_path;

        char options_buffer[4096];
        *options.MakeId(options_buffer) = 0;
        if (*options_buffer != 0)
            key = p_strcat(&pool, key, "|", options_buffer, nullptr);

        return key;
    }
};

class DelegateProcess final : public HeapStockItem {
    const int fd;

    SocketEvent event;

public:
    explicit DelegateProcess(CreateStockItem c, int _fd)
        :HeapStockItem(c), fd(_fd),
         event(c.stock.GetEventLoop(), fd, EV_READ,
               BIND_THIS_METHOD(SocketEventCallback)) {
    }

    ~DelegateProcess() override {
        if (fd >= 0) {
            event.Delete();
            close(fd);
        }
    }

    int GetSocket() const {
        return fd;
    }

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        event.Add(EventDuration<60>::value);
        return true;
    }

private:
    void SocketEventCallback(unsigned events);
};

/*
 * libevent callback
 *
 */

inline void
DelegateProcess::SocketEventCallback(unsigned events)
{
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
delegate_stock_create(void *ctx,
                      CreateStockItem c,
                      void *_info,
                      gcc_unused struct pool &caller_pool,
                      gcc_unused CancellablePointer &cancel_ptr)
{
    auto &spawn_service = *(SpawnService *)ctx;
    auto &info = *(DelegateArgs *)_info;

    PreparedChildProcess p;
    p.Append(info.executable_path);

    try {
        info.options.CopyTo(p, true, nullptr);
    } catch (const std::runtime_error &e) {
        c.InvokeCreateError(ToGError(e));
        return;
    }

    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        auto *error = new_error_errno_msg("socketpair() failed: %s");
        c.InvokeCreateError(error);
        return;
    }

    p.SetStdin(fds[1]);

    try {
        spawn_service.SpawnChildProcess(info.executable_path,
                                        std::move(p), nullptr);

        auto *process = new DelegateProcess(c, fds[0]);
        process->InvokeCreateSuccess();
    } catch (const std::runtime_error &e) {
        close(fds[0]);
        c.InvokeCreateError(ToGError(e));
    }
}

static constexpr StockClass delegate_stock_class = {
    .create = delegate_stock_create,
};


/*
 * interface
 *
 */

StockMap *
delegate_stock_new(EventLoop &event_loop, SpawnService &spawn_service)
{
    return new StockMap(event_loop, delegate_stock_class, &spawn_service,
                        0, 16);
}

StockItem *
delegate_stock_get(StockMap *delegate_stock, struct pool *pool,
                   const char *helper,
                   const ChildOptions &options,
                   GError **error_r)
{
    DelegateArgs args(helper, options);
    return delegate_stock->GetNow(*pool, args.GetStockKey(*pool),
                                  &args, error_r);
}

int
delegate_stock_item_get(StockItem &item)
{
    auto *process = (DelegateProcess *)&item;

    return process->GetSocket();
}
