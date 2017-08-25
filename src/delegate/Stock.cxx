/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "failure.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "event/Duration.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
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

        char options_buffer[16384];
        *options.MakeId(options_buffer) = 0;
        if (*options_buffer != 0)
            key = p_strcat(&pool, key, "|", options_buffer, nullptr);

        return key;
    }
};

class DelegateProcess final : public StockItem {
    UniqueSocketDescriptor fd;

    SocketEvent event;

public:
    explicit DelegateProcess(CreateStockItem c, UniqueSocketDescriptor &&_fd)
        :StockItem(c), fd(std::move(_fd)),
         event(c.stock.GetEventLoop(), fd.Get(), SocketEvent::READ,
               BIND_THIS_METHOD(SocketEventCallback)) {
    }

    ~DelegateProcess() override {
        if (fd.IsDefined())
            event.Delete();
    }

    int GetSocket() const {
        return fd.Get();
    }

    /* virtual methods from class StockItem */
    bool Borrow() override {
        event.Delete();
        return true;
    }

    bool Release() override {
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
    if ((events & SocketEvent::TIMEOUT) == 0) {
        assert((events & SocketEvent::READ) != 0);

        char buffer;
        ssize_t nbytes = recv(fd.Get(), &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle delegate process: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle delegate process\n");
    }

    InvokeIdleDisconnect();
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

    info.options.CopyTo(p, true, nullptr);

    UniqueSocketDescriptor server_fd, client_fd;
    if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                                  server_fd, client_fd))
        throw MakeErrno("socketpair() failed");

    p.SetStdin(std::move(server_fd));

    spawn_service.SpawnChildProcess(info.executable_path,
                                    std::move(p), nullptr);

    auto *process = new DelegateProcess(c, std::move(client_fd));
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
delegate_stock_new(EventLoop &event_loop, SpawnService &spawn_service)
{
    return new StockMap(event_loop, delegate_stock_class, &spawn_service,
                        0, 16);
}

StockItem *
delegate_stock_get(StockMap *delegate_stock, struct pool *pool,
                   const char *helper,
                   const ChildOptions &options)
{
    DelegateArgs args(helper, options);
    return delegate_stock->GetNow(*pool, args.GetStockKey(*pool), &args);
}

int
delegate_stock_item_get(StockItem &item)
{
    auto *process = (DelegateProcess *)&item;

    return process->GetSocket();
}
