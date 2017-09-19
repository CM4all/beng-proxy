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
#include "Factory.hxx"
#include "FilteredSocket.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/LoggerDomain.hxx"
#include "address_list.hxx"
#include "pool/pool.hxx"
#include "net/PConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/ToString.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct FilteredSocketStockRequest {
    struct pool &caller_pool;

    const bool ip_transparent;

    const SocketAddress bind_address, address;

    const Event::Duration timeout;

    SocketFilterFactory *const filter_factory;

    FilteredSocketStockRequest(struct pool &_caller_pool,
                               bool _ip_transparent,
                               SocketAddress _bind_address,
                               SocketAddress _address,
                               Event::Duration _timeout,
                               SocketFilterFactory *_filter_factory)
        :caller_pool(_caller_pool),
         ip_transparent(_ip_transparent),
         bind_address(_bind_address), address(_address),
         timeout(_timeout),
         filter_factory(_filter_factory) {}
};

struct FilteredSocketStockConnection final
    : StockItem, ConnectSocketHandler, BufferedSocketHandler, Cancellable {

    BasicLogger<StockLoggerDomain> logger;

    const FdType type;

    const AllocatedSocketAddress address;

    SocketFilterFactory *const filter_factory;

    /**
     * To cancel the ClientSocket.
     */
    CancellablePointer cancel_ptr;

    FilteredSocket socket;

    FilteredSocketStockConnection(CreateStockItem c, FdType _type,
                                  SocketAddress _address,
                                  SocketFilterFactory *_filter_factory,
                                  CancellablePointer &_cancel_ptr) noexcept
        :StockItem(c),
         logger(c.stock),
         type(_type),
         address(_address),
         filter_factory(_filter_factory),
         socket(c.stock.GetEventLoop()) {
        _cancel_ptr = *this;

        cancel_ptr = nullptr;
    }

    ~FilteredSocketStockConnection() override {
        if (cancel_ptr)
            cancel_ptr.Cancel();
        else if (socket.IsValid() && socket.IsConnected()) {
            socket.Close();
            socket.Destroy();
        }
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        assert(cancel_ptr);

        cancel_ptr.CancelAndClear();
        InvokeCreateAborted();
    }

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept override;
    void OnSocketConnectError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData() override;
    bool OnBufferedClosed() noexcept override;

    gcc_noreturn
    bool OnBufferedWrite() override {
        /* should never be reached because we never schedule
           writing */
        gcc_unreachable();
    }

    void OnBufferedError(std::exception_ptr e) noexcept override;

    /* virtual methods from class StockItem */
    bool Borrow() noexcept override {
        return true;
    }

    bool Release() noexcept override;
};

/*
 * BufferedSocketHandler
 *
 */

BufferedResult
FilteredSocketStockConnection::OnBufferedData()
{
    logger(2, "unexpected data in idle TCP connection");
    InvokeIdleDisconnect();
    return BufferedResult::CLOSED;
}

bool
FilteredSocketStockConnection::OnBufferedClosed() noexcept
{
    InvokeIdleDisconnect();
    return false;
}

void
FilteredSocketStockConnection::OnBufferedError(std::exception_ptr e) noexcept
{
    logger(2, "error on idle connection: ", e);
    InvokeIdleDisconnect();
}

/*
 * client_socket callback
 *
 */

void
FilteredSocketStockConnection::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept
{
    cancel_ptr = nullptr;

    try {
        socket.Init(fd.Release(), type,
                    Event::Duration(-1), Event::Duration(-1),
                    filter_factory != nullptr
                    ? filter_factory->CreateFilter()
                    : nullptr,
                    *this);
    } catch (...) {
        InvokeCreateError(std::current_exception());
        return;
    }

    InvokeCreateSuccess();
}

void
FilteredSocketStockConnection::OnSocketConnectError(std::exception_ptr ep) noexcept
{
    cancel_ptr = nullptr;

    ep = NestException(ep,
                       FormatRuntimeError("Failed to connect to '%s'",
                                          GetStockName()));
    InvokeCreateError(ep);
}

/*
 * stock class
 *
 */

void
FilteredSocketStock::Create(CreateStockItem c, void *info,
                            CancellablePointer &cancel_ptr)
{
    const auto &request = *(const FilteredSocketStockRequest *)info;

    const int address_family = request.address.GetFamily();
    const FdType type = address_family == AF_LOCAL
        ? FD_SOCKET
        : FD_TCP;

    auto *connection = new FilteredSocketStockConnection(c, type,
                                                         request.address,
                                                         request.filter_factory,
                                                         cancel_ptr);

    client_socket_new(c.stock.GetEventLoop(), request.caller_pool,
                      address_family, SOCK_STREAM, 0,
                      request.ip_transparent,
                      request.bind_address,
                      request.address,
                      request.timeout,
                      *connection,
                      connection->cancel_ptr);
}

bool
FilteredSocketStockConnection::Release() noexcept
{
    if (!socket.IsConnected())
        return false;

    if (!socket.IsEmpty()) {
        logger(2, "unexpected data in idle connection");
        return false;
    }

    socket.Reinit(Event::Duration(-1), Event::Duration(-1), *this);
    socket.UnscheduleWrite();

    socket.ScheduleReadTimeout(false, std::chrono::minutes(1));

    return true;
}

/*
 * interface
 *
 */

void
FilteredSocketStock::Get(struct pool &pool, const char *name,
                         bool ip_transparent,
                         SocketAddress bind_address,
                         SocketAddress address,
                         Event::Duration timeout,
                         SocketFilterFactory *filter_factory,
                         StockGetHandler &handler,
                         CancellablePointer &cancel_ptr) noexcept
{
    assert(!address.IsNull());

    auto request =
        NewFromPool<FilteredSocketStockRequest>(pool, pool, ip_transparent,
                                                bind_address, address,
                                                timeout, filter_factory);

    if (name == nullptr) {
        char buffer[1024];
        if (!ToString(buffer, sizeof(buffer), address))
            buffer[0] = 0;

        if (!bind_address.IsNull()) {
            char bind_buffer[1024];
            if (!ToString(bind_buffer, sizeof(bind_buffer), bind_address))
                bind_buffer[0] = 0;
            name = p_strcat(&pool, bind_buffer, ">", buffer, nullptr);
        } else
            name = p_strdup(&pool, buffer);
    }

    if (filter_factory != nullptr)
        name = p_strcat(&pool, name, "|", filter_factory->GetFilterId(),
                        nullptr);

    stock.Get(name, request, handler, cancel_ptr);
}

FilteredSocket &
fs_stock_item_get(StockItem &item)
{
    auto &connection = (FilteredSocketStockConnection &)item;

    return connection.socket;
}

SocketAddress
fs_stock_item_get_address(const StockItem &item)
{
    const auto &connection = (const FilteredSocketStockConnection &)item;

    return connection.address;
}
