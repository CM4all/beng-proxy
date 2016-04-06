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

#include <was/protocol.h>
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

static constexpr struct timeval was_idle_timeout = {
    .tv_sec = 300,
    .tv_usec = 0,
};

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

class WasChild final : public HeapStockItem, ExitListener {
    SpawnService &spawn_service;

    WasProcess process;
    Event event;

    /**
     * If true, then we're waiting for PREMATURE (after the #WasClient
     * has sent #WAS_COMMAND_STOP).
     */
    bool stopping = false;

    /**
     * The number of bytes received before #WAS_COMMAND_STOP was sent.
     */
    uint64_t input_received;

public:
    explicit WasChild(CreateStockItem c, SpawnService &_spawn_service)
        :HeapStockItem(c), spawn_service(_spawn_service) {}

    ~WasChild() override;

    bool Launch(const WasChildParams &params, GError **error_r) {
        if (!was_launch(spawn_service, &process,
                        GetStockName(),
                        params.executable_path,
                        params.args,
                        params.options,
                        this,
                        error_r))
            return false;

        event.Set(process.control_fd, EV_READ|EV_TIMEOUT,
                  MakeEventCallback(WasChild, EventCallback), this);
        return true;
    }

    const WasProcess &GetProcess() const {
        return process;
    }

    void Stop(uint64_t _received) {
        assert(!is_idle);
        assert(!stopping);

        stopping = true;
        input_received = _received;
    }

private:
    /**
     * Receive data on the control channel.
     *
     * @return true on success
     */
    bool ReceiveControl(void *p, size_t size);

    /**
     * Discard the given amount of data from the input pipe.
     *
     * @return true on success
     */
    bool DiscardInput(uint64_t remaining);

    /**
     * Attempt to recover after the WAS client sent STOP to the
     * application.  This method waits for PREMATURE and discards
     * excess data from the pipe.
     */
    void RecoverStop();

    void EventCallback(evutil_socket_t fd, short events);

public:
    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        if (stopping)
            /* we havn't yet recovered from #WAS_COMMAND_STOP - give
               up this child process */
            // TODO: improve recovery for this case
            return false;

        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        event.Add(was_idle_timeout);
        return true;
    }

private:
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

inline bool
WasChild::ReceiveControl(void *p, size_t size)
{
    ssize_t nbytes = recv(process.control_fd, p, size, MSG_DONTWAIT);
    if (nbytes == (ssize_t)size)
        return true;

    if (nbytes < 0 && errno == EAGAIN) {
        /* the WAS application didn't send enough data (yet); don't
           bother waiting for more, just give up on this process */
        return false;
    }

    if (nbytes < 0)
        daemon_log(2, "error on idle WAS control connection '%s': %s\n",
                   GetStockName(), strerror(errno));
    else if (nbytes > 0)
        daemon_log(2, "unexpected data from idle WAS control connection '%s'\n",
                   GetStockName());
    return false;
}

inline bool
WasChild::DiscardInput(uint64_t remaining)
{
    while (remaining > 0) {
        uint8_t buffer[16384];
        size_t size = std::min(remaining, uint64_t(sizeof(buffer)));
        ssize_t nbytes = read(process.input_fd, buffer, size);
        if (nbytes <= 0)
            return false;

        remaining -= nbytes;
    }

    return true;
}

inline void
WasChild::RecoverStop()
{
    uint64_t premature;

    while (true) {
        struct was_header header;
        if (!ReceiveControl(&header, sizeof(header))) {
            InvokeIdleDisconnect();
            return;
        }

        uint64_t dummy;

        switch ((enum was_command)header.command) {
        case WAS_COMMAND_NOP:
            /* ignore */
            continue;

        case WAS_COMMAND_LENGTH:
        case WAS_COMMAND_STOP:
            /* discard & ignore */
            if (!ReceiveControl(&dummy, sizeof(dummy))) {
                InvokeIdleDisconnect();
                return;
            }
            continue;

        case WAS_COMMAND_REQUEST:
        case WAS_COMMAND_METHOD:
        case WAS_COMMAND_URI:
        case WAS_COMMAND_SCRIPT_NAME:
        case WAS_COMMAND_PATH_INFO:
        case WAS_COMMAND_QUERY_STRING:
        case WAS_COMMAND_HEADER:
        case WAS_COMMAND_PARAMETER:
        case WAS_COMMAND_STATUS:
        case WAS_COMMAND_NO_DATA:
        case WAS_COMMAND_DATA:
            daemon_log(2, "unexpected data from idle WAS control connection '%s'\n",
                       GetStockName());
            InvokeIdleDisconnect();
            return;

        case WAS_COMMAND_PREMATURE:
            /* this is what we're waiting for */
            break;
        }

        if (!ReceiveControl(&premature, sizeof(premature))) {
            InvokeIdleDisconnect();
            return;
        }

        break;
    }

    if (premature < input_received) {
        InvokeIdleDisconnect();
        return;
    }

    if (!DiscardInput(premature - input_received)) {
        InvokeIdleDisconnect();
        return;
    }

    stopping = false;

    event.Add(was_idle_timeout);
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
        if (stopping) {
            RecoverStop();
            return;
        }

        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle WAS control connection '%s': %s\n",
                       GetStockName(), strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle WAS control connection '%s'\n",
                       GetStockName());
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
    if (!child->Launch(*params, &error)) {
        child->InvokeCreateError(error);
        return;
    }

    child->InvokeCreateSuccess();
}

WasChild::~WasChild()
{
    if (process.pid >= 0)
        spawn_service.KillChildProcess(process.pid);

    if (process.control_fd >= 0)
        event.Delete();
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

    return child->GetProcess();
}

void
was_stock_item_stop(StockItem &item, uint64_t received)
{
    auto &child = (WasChild &)item;
    child.Stop(received);
}
