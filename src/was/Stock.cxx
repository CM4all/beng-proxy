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
#include "Launch.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "pool.hxx"
#include "event/SocketEvent.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cast.hxx"

#include <was/protocol.h>

#include <string>

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

class WasChild final : public StockItem, ExitListener {
    const LLogger logger;

    SpawnService &spawn_service;

    const std::string tag;

    WasProcess process;
    SocketEvent event;

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
    explicit WasChild(CreateStockItem c, SpawnService &_spawn_service,
                      const char *_tag)
        :StockItem(c), logger(GetStockName()), spawn_service(_spawn_service),
         tag(_tag != nullptr ? _tag : ""),
         event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)) {
        /* mark this object as "unused" so the destructor doesn't
           attempt to kill the process */
        process.pid = -1;
    }

    ~WasChild() override;

    bool IsTag(const char *other_tag) const {
        return tag == other_tag;
    }

    void Launch(const WasChildParams &params) {
        process = was_launch(spawn_service,
                             GetStockName(),
                             params.executable_path,
                             params.args,
                             params.options,
                             this);
        event.Set(process.control.Get(), SocketEvent::READ);
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
    enum class ReceiveResult {
        SUCCESS, ERROR, AGAIN,
    };

    /**
     * Receive data on the control channel.
     */
    ReceiveResult ReceiveControl(void *p, size_t size);

    /**
     * Receive and discard data on the control channel.
     *
     * @return true on success
     */
    bool DiscardControl(size_t size);

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

    void EventCallback(unsigned events);

public:
    /* virtual methods from class StockItem */
    bool Borrow() override {
        if (stopping)
            /* we havn't yet recovered from #WAS_COMMAND_STOP - give
               up this child process */
            // TODO: improve recovery for this case
            return false;

        event.Delete();
        return true;
    }

    bool Release() override {
        event.Add(was_idle_timeout);
        unclean = stopping;
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

    char options_buffer[16384];
    *options.MakeId(options_buffer) = 0;
    if (*options_buffer != 0)
        key = p_strcat(&pool, key, options_buffer, nullptr);

    return key;
}

WasChild::ReceiveResult
WasChild::ReceiveControl(void *p, size_t size)
{
    ssize_t nbytes = recv(process.control.Get(), p, size, MSG_DONTWAIT);
    if (nbytes == (ssize_t)size)
        return ReceiveResult::SUCCESS;

    if (nbytes < 0 && errno == EAGAIN) {
        /* the WAS application didn't send enough data (yet); don't
           bother waiting for more, just give up on this process */
        return ReceiveResult::AGAIN;
    }

    if (nbytes < 0)
        logger(2, "error on idle WAS control connection: ", strerror(errno));
    else if (nbytes > 0)
        logger(2, "unexpected data from idle WAS control connection");
    return ReceiveResult::ERROR;
}

bool
WasChild::DiscardControl(size_t size)
{
    while (size > 0) {
        char buffer[1024];
        ssize_t nbytes = recv(process.control.Get(), buffer,
                              std::min(size, sizeof(buffer)),
                              MSG_DONTWAIT);
        if (nbytes <= 0)
            return false;

        size -= nbytes;
    }

    return true;
}

inline bool
WasChild::DiscardInput(uint64_t remaining)
{
    while (remaining > 0) {
        uint8_t buffer[16384];
        size_t size = std::min(remaining, uint64_t(sizeof(buffer)));
        ssize_t nbytes = process.input.Read(buffer, size);
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
        switch (ReceiveControl(&header, sizeof(header))) {
        case ReceiveResult::SUCCESS:
            break;

        case ReceiveResult::ERROR:
            InvokeIdleDisconnect();
            return;

        case ReceiveResult::AGAIN:
            /* wait for more data */
            event.Add(was_idle_timeout);
            return;
        }

        switch ((enum was_command)header.command) {
        case WAS_COMMAND_NOP:
            /* ignore */
            continue;

        case WAS_COMMAND_HEADER:
        case WAS_COMMAND_STATUS:
        case WAS_COMMAND_NO_DATA:
        case WAS_COMMAND_DATA:
        case WAS_COMMAND_LENGTH:
        case WAS_COMMAND_STOP:
            /* discard & ignore */
            if (!DiscardControl(header.length)) {
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
        case WAS_COMMAND_PARAMETER:
            logger(2, "unexpected data from idle WAS control connection '",
                      GetStockName(), "'");
            InvokeIdleDisconnect();
            return;

        case WAS_COMMAND_PREMATURE:
            /* this is what we're waiting for */
            break;
        }

        if (ReceiveControl(&premature, sizeof(premature)) != ReceiveResult::SUCCESS) {
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
    unclean = false;

    event.Add(was_idle_timeout);
}

/*
 * libevent callback
 *
 */

inline void
WasChild::EventCallback(unsigned events)
{
    if ((events & SocketEvent::TIMEOUT) == 0) {
        if (stopping) {
            RecoverStop();
            return;
        }

        char buffer;
        ssize_t nbytes = recv(process.control.Get(), &buffer, sizeof(buffer),
                              MSG_DONTWAIT);
        if (nbytes < 0)
            logger(2, "error on idle WAS control connection: ",
                   strerror(errno));
        else if (nbytes > 0)
            logger(2, "unexpected data from idle WAS control connection");
    }

    InvokeIdleDisconnect();
}

/*
 * stock class
 *
 */

class WasStock final : StockClass {
    SpawnService &spawn_service;
    StockMap stock;

public:
    explicit WasStock(EventLoop &event_loop, SpawnService &_spawn_service,
                      unsigned limit, unsigned max_idle)
        :spawn_service(_spawn_service),
         stock(event_loop, *this, limit, max_idle) {}

    StockMap &GetStock() {
        return stock;
    }

    void FadeTag(const char *tag) {
        stock.FadeIf([tag](const StockItem &item){
                const auto &child = (const WasChild &)item;
                return child.IsTag(tag);
            });
    }

private:
    /* virtual methods from class StockClass */
    void Create(CreateStockItem c, void *info, struct pool &caller_pool,
                CancellablePointer &cancel_ptr) override;
};

void
WasStock::Create(CreateStockItem c,
                 void *info,
                 gcc_unused struct pool &caller_pool,
                 gcc_unused CancellablePointer &cancel_ptr)
{
    WasChildParams *params = (WasChildParams *)info;

    auto *child = new WasChild(c, spawn_service, params->options.tag);

    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    try {
        child->Launch(*params);
    } catch (...) {
        child->InvokeCreateError(std::current_exception());
        return;
    }

    child->InvokeCreateSuccess();
}

WasChild::~WasChild()
{
    if (process.pid >= 0)
        spawn_service.KillChildProcess(process.pid);

    if (process.control.IsDefined())
        event.Delete();
}

/*
 * interface
 *
 */

StockMap *
was_stock_new(unsigned limit, unsigned max_idle,
              EventLoop &event_loop, SpawnService &spawn_service)
{
    auto *stock = new WasStock(event_loop, spawn_service, limit, max_idle);
    return &stock->GetStock();
}

void
was_stock_free(StockMap *_stock)
{
    auto *stock = (WasStock *)&_stock->GetClass();
    delete stock;
}

void
was_stock_fade_tag(StockMap &_stock, const char *tag)
{
    auto &stock = (WasStock &)_stock.GetClass();
    stock.FadeTag(tag);
}

void
was_stock_get(StockMap *hstock, struct pool *pool,
              const ChildOptions &options,
              const char *executable_path,
              ConstBuffer<const char *> args,
              StockGetHandler &handler,
              CancellablePointer &cancel_ptr)
{
    auto params = NewFromPool<WasChildParams>(*pool, executable_path, args,
                                              options);

    hstock->Get(*pool, params->GetStockKey(*pool), params,
                handler, cancel_ptr);
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
