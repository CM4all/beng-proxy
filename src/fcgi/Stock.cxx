/*
 * Copyright 2007-2018 Content Management AG
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
#include "Error.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "child_stock.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/JailParams.hxx"
#include "spawn/JailConfig.hxx"
#include "pool/tpool.hxx"
#include "pool/StringBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/ConstBuffer.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"
#include "util/StringFormat.hxx"

#include <string>

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

struct FcgiStock final : StockClass, ChildStockClass {
    StockMap hstock;
    ChildStock child_stock;

    FcgiStock(unsigned limit, unsigned max_idle,
              EventLoop &event_loop, SpawnService &spawn_service,
              SocketDescriptor _log_socket) noexcept;

    ~FcgiStock() {
        /* this one must be cleared before #child_stock; FadeAll()
           calls ClearIdle(), so this method is the best match for
           what we want to do (though a kludge) */
        hstock.FadeAll();
    }

    EventLoop &GetEventLoop() {
        return hstock.GetEventLoop();
    }

    SocketDescriptor GetLogSocket() const noexcept {
        return child_stock.GetLogSocket();
    }

    void FadeAll() {
        hstock.FadeAll();
        child_stock.GetStockMap().FadeAll();
    }

    void FadeTag(const char *tag);

    /* virtual methods from class StockClass */
    void Create(CreateStockItem c, void *info, struct pool &caller_pool,
                CancellablePointer &cancel_ptr) override;

    /* virtual methods from class ChildStockClass */
    const char *GetChildTag(void *info) const noexcept override;
    void PrepareChild(void *info, UniqueSocketDescriptor &&fd,
                      PreparedChildProcess &p) override;
};

struct FcgiChildParams {
    const char *executable_path;

    ConstBuffer<const char *> args;

    const ChildOptions &options;

    FcgiChildParams(const char *_executable_path,
                    ConstBuffer<const char *> _args,
                    const ChildOptions &_options)
        :executable_path(_executable_path), args(_args),
         options(_options) {}

    const char *GetStockKey(struct pool &pool) const;
};

struct FcgiConnection final : StockItem {
    const LLogger logger;

    std::string jail_home_directory;

    JailConfig jail_config;

    StockItem *child = nullptr;

    UniqueSocketDescriptor fd;
    SocketEvent event;
    TimerEvent idle_timeout_event;

    /**
     * Is this a fresh connection to the FastCGI child process?
     */
    bool fresh = true;

    /**
     * Shall the FastCGI child process be killed?
     */
    bool kill = false;

    /**
     * Was the current request aborted by the fcgi_client caller?
     */
    bool aborted = false;

    explicit FcgiConnection(EventLoop &event_loop, CreateStockItem c) noexcept
        :StockItem(c), logger(GetStockName()),
         event(event_loop, BIND_THIS_METHOD(OnSocketEvent)),
         idle_timeout_event(c.stock.GetEventLoop(),
                            BIND_THIS_METHOD(OnIdleTimeout)) {}

    ~FcgiConnection() noexcept override;

    gcc_pure
    const char *GetTag() const {
        assert(child != nullptr);

        return child_stock_item_get_tag(*child);
    }

    void SetSite(const char *site) noexcept {
        child_stock_item_set_site(*child, site);
    }

    void SetUri(const char *uri) noexcept {
        child_stock_item_set_uri(*child, uri);
    }

    /* virtual methods from class StockItem */
    bool Borrow() noexcept override;
    bool Release() noexcept override;

private:
    void OnSocketEvent(unsigned events) noexcept;
    void OnIdleTimeout() noexcept;
};

const char *
FcgiChildParams::GetStockKey(struct pool &pool) const
{
    PoolStringBuilder<256> b;
    b.push_back(executable_path);

    for (auto i : args) {
        b.push_back(" ");
        b.push_back(i);
    }

    for (auto i : options.env) {
        b.push_back("$");
        b.push_back(i);
    }

    char options_buffer[16384];
    b.emplace_back(options_buffer,
                   options.MakeId(options_buffer));

    return b(pool);
}

/*
 * libevent callback
 *
 */

void
FcgiConnection::OnSocketEvent(unsigned) noexcept
{
    char buffer;
    ssize_t nbytes = fd.Read(&buffer, sizeof(buffer));
    if (nbytes < 0)
        logger(2, "error on idle FastCGI connection: ", strerror(errno));
    else if (nbytes > 0)
        logger(2, "unexpected data from idle FastCGI connection");

    InvokeIdleDisconnect();
}

inline void
FcgiConnection::OnIdleTimeout() noexcept
{
    InvokeIdleDisconnect();
}

/*
 * child_stock class
 *
 */

const char *
FcgiStock::GetChildTag(void *info) const noexcept
{
    const auto &params = *(const FcgiChildParams *)info;

    return params.options.tag;
}

void
FcgiStock::PrepareChild(void *info, UniqueSocketDescriptor &&fd,
                        PreparedChildProcess &p)
{
    auto &params = *(FcgiChildParams *)info;
    const ChildOptions &options = params.options;

    p.SetStdin(std::move(fd));

    /* the FastCGI protocol defines a channel for stderr, so we could
       close its "real" stderr here, but many FastCGI applications
       don't use the FastCGI protocol to send error messages, so we
       just keep it open */

    UniqueFileDescriptor null_fd;
    if (null_fd.Open("/dev/null", O_WRONLY))
        p.SetStdout(std::move(null_fd));

    p.Append(params.executable_path);
    for (auto i : params.args)
        p.Append(i);

    options.CopyTo(p, true, nullptr);
}

/*
 * stock class
 *
 */

void
FcgiStock::Create(CreateStockItem c, void *info,
                  struct pool &caller_pool,
                  gcc_unused CancellablePointer &cancel_ptr)
{
    FcgiChildParams *params = (FcgiChildParams *)info;

    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    auto *connection = new FcgiConnection(GetEventLoop(), c);

    const ChildOptions &options = params->options;
    if (options.jail != nullptr && options.jail->enabled) {
        connection->jail_home_directory = options.jail->home_directory;

        if (!connection->jail_config.Load("/etc/cm4all/jailcgi/jail.conf")) {
            delete connection;
            throw FcgiClientError("Failed to load /etc/cm4all/jailcgi/jail.conf");
        }
    }

    const char *key = c.GetStockName();

    try {
        connection->child = child_stock.GetStockMap().GetNow(caller_pool, key, params);
    } catch (...) {
        delete connection;
        std::throw_with_nested(FcgiClientError(StringFormat<256>("Failed to start FastCGI server '%s'",
                                                                 key)));
    }

    try {
        connection->fd = child_stock_item_connect(*connection->child);
    } catch (...) {
        connection->kill = true;
        delete connection;
        std::throw_with_nested(FcgiClientError(StringFormat<256>("Failed to connect to FastCGI server '%s'",
                                                                 key)));
    }

    connection->event.Open(connection->fd);

    connection->InvokeCreateSuccess();
}

bool
FcgiConnection::Borrow() noexcept
{
    /* check the connection status before using it, just in case the
       FastCGI server has decided to close the connection before
       fcgi_connection_event_callback() got invoked */
    char buffer;
    ssize_t nbytes = fd.Read(&buffer, sizeof(buffer));
    if (nbytes > 0) {
        logger(2, "unexpected data from idle FastCGI connection");
        return false;
    } else if (nbytes == 0) {
        /* connection closed (not worth a log message) */
        return false;
    } else if (errno != EAGAIN) {
        logger(2, "error on idle FastCGI connection: ", strerror(errno));
        return false;
    }

    event.Cancel();
    idle_timeout_event.Cancel();
    aborted = false;
    return true;
}

bool
FcgiConnection::Release() noexcept
{
    fresh = false;
    event.ScheduleRead();
    idle_timeout_event.Schedule(std::chrono::minutes(6));
    return true;
}

FcgiConnection::~FcgiConnection() noexcept
{
    if (fd.IsDefined()) {
        event.Cancel();
        fd.Close();
    }

    if (fresh && aborted)
        /* the fcgi_client caller has aborted the request before the
           first response on a fresh connection was received: better
           kill the child process, it may be failing on us
           completely */
        kill = true;

    if (child != nullptr)
        child->Put(kill);
}


/*
 * interface
 *
 */

inline
FcgiStock::FcgiStock(unsigned limit, unsigned max_idle,
                     EventLoop &event_loop, SpawnService &spawn_service,
                     SocketDescriptor _log_socket) noexcept
    :hstock(event_loop, *this, limit, max_idle),
     child_stock(event_loop, spawn_service,
                 *this,
                 _log_socket,
                 limit, max_idle) {}

void
FcgiStock::FadeTag(const char *tag)
{
    assert(tag != nullptr);

    hstock.FadeIf([tag](const StockItem &item){
            const auto &connection = (const FcgiConnection &)item;
            const char *tag2 = connection.GetTag();
            return tag2 != nullptr && strcmp(tag, tag2) == 0;
        });

    child_stock.FadeTag(tag);
}

FcgiStock *
fcgi_stock_new(unsigned limit, unsigned max_idle,
               EventLoop &event_loop, SpawnService &spawn_service,
               SocketDescriptor log_socket)
{
    return new FcgiStock(limit, max_idle, event_loop, spawn_service,
                         log_socket);
}

void
fcgi_stock_free(FcgiStock *fcgi_stock)
{
    delete fcgi_stock;
}

SocketDescriptor
fcgi_stock_get_log_socket(const FcgiStock &fs) noexcept
{
    return fs.GetLogSocket();
}

void
fcgi_stock_fade_all(FcgiStock &fs)
{
    fs.FadeAll();
}

void
fcgi_stock_fade_tag(FcgiStock &fs, const char *tag)
{
    fs.FadeTag(tag);
}

StockItem *
fcgi_stock_get(FcgiStock *fcgi_stock,
               const ChildOptions &options,
               const char *executable_path,
               ConstBuffer<const char *> args)
{
    const AutoRewindPool auto_rewind(*tpool);

    auto params = NewFromPool<FcgiChildParams>(*tpool, executable_path,
                                               args, options);

    return fcgi_stock->hstock.GetNow(*tpool,
                                     params->GetStockKey(*tpool), params);
}

int
fcgi_stock_item_get_domain(gcc_unused const StockItem &item)
{
    return AF_LOCAL;
}

void
fcgi_stock_item_set_site(StockItem &item, const char *site) noexcept
{
    auto &connection = (FcgiConnection &)item;
    connection.SetSite(site);
}

void
fcgi_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
    auto &connection = (FcgiConnection &)item;
    connection.SetUri(uri);
}

SocketDescriptor
fcgi_stock_item_get(const StockItem &item)
{
    const auto *connection = (const FcgiConnection *)&item;

    assert(connection->fd.IsDefined());

    return connection->fd;
}

const char *
fcgi_stock_translate_path(const StockItem &item,
                          const char *path, AllocatorPtr alloc)
{
    const auto *connection = (const FcgiConnection *)&item;

    if (connection->jail_home_directory.empty())
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = connection->jail_config.TranslatePath(path,
                                                               connection->jail_home_directory.c_str(),
                                                               alloc);
    return jailed != nullptr ? jailed : path;
}

void
fcgi_stock_aborted(StockItem &item)
{
    auto *connection = (FcgiConnection *)&item;

    connection->aborted = true;
}
