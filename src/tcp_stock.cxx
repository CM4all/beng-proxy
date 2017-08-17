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

#include "tcp_stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "address_list.hxx"
#include "pool.hxx"
#include "event/SocketEvent.hxx"
#include "event/Duration.hxx"
#include "net/PConnectSocket.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/ToString.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct TcpStockRequest {
    const bool ip_transparent;

    const SocketAddress bind_address, address;

    const unsigned timeout;

    TcpStockRequest(bool _ip_transparent, SocketAddress _bind_address,
                    SocketAddress _address, unsigned _timeout)
        :ip_transparent(_ip_transparent), bind_address(_bind_address),
         address(_address), timeout(_timeout) {}
};

class StockLoggerDomain {
    const Stock &stock;

public:
    explicit StockLoggerDomain(const Stock &_stock):stock(_stock) {}

    StringView GetDomain() const {
        return stock.GetName();
    }
};

struct TcpStockConnection final
    : HeapStockItem, ConnectSocketHandler, Cancellable {

    BasicLogger<StockLoggerDomain> logger;

    /**
     * To cancel the ClientSocket.
     */
    CancellablePointer cancel_ptr;

    SocketDescriptor fd = SocketDescriptor::Undefined();

    const AllocatedSocketAddress address;

    SocketEvent event;

    TcpStockConnection(CreateStockItem c, SocketAddress _address,
                       CancellablePointer &_cancel_ptr)
        :HeapStockItem(c),
         logger(c.stock),
         address(_address),
         event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)) {
        _cancel_ptr = *this;

        cancel_ptr = nullptr;
    }

    ~TcpStockConnection() override;

    void EventCallback(unsigned events);

    /* virtual methods from class Cancellable */
    void Cancel() override {
        assert(cancel_ptr);

        cancel_ptr.CancelAndClear();
        InvokeCreateAborted();
    }

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) override;
    void OnSocketConnectError(std::exception_ptr ep) override;

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        event.Add(EventDuration<60>::value);
        return true;
    }
};


/*
 * libevent callback
 *
 */

inline void
TcpStockConnection::EventCallback(unsigned events)
{
    if ((events & SocketEvent::TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes;

        assert((events & SocketEvent::READ) != 0);

        nbytes = fd.Read(&buffer, sizeof(buffer));
        if (nbytes < 0)
            logger(2, "error on idle TCP connection: ",strerror(errno));
        else if (nbytes > 0)
            logger(2, "unexpected data in idle TCP connection");
    }

    InvokeIdleDisconnect();
}


/*
 * client_socket callback
 *
 */

void
TcpStockConnection::OnSocketConnectSuccess(UniqueSocketDescriptor &&new_fd)
{
    cancel_ptr = nullptr;

    fd = new_fd.Release();
    event.Set(fd.Get(), SocketEvent::READ);

    InvokeCreateSuccess();
}

void
TcpStockConnection::OnSocketConnectError(std::exception_ptr ep)
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

static void
tcp_stock_create(gcc_unused void *ctx,
                 CreateStockItem c,
                 void *info,
                 struct pool &caller_pool,
                 CancellablePointer &cancel_ptr)
{
    TcpStockRequest *request = (TcpStockRequest *)info;

    auto *connection = new TcpStockConnection(c,
                                              request->address,
                                              cancel_ptr);

    client_socket_new(c.stock.GetEventLoop(), caller_pool,
                      request->address.GetFamily(), SOCK_STREAM, 0,
                      request->ip_transparent,
                      request->bind_address,
                      request->address,
                      request->timeout,
                      *connection,
                      connection->cancel_ptr);
}

TcpStockConnection::~TcpStockConnection()
{
    if (cancel_ptr)
        cancel_ptr.Cancel();
    else if (fd.IsDefined()) {
        event.Delete();
        fd.Close();
    }
}

static constexpr StockClass tcp_stock_class = {
    .create = tcp_stock_create,
};


/*
 * interface
 *
 */

StockMap *
tcp_stock_new(EventLoop &event_loop, unsigned limit)
{
    return new StockMap(event_loop, tcp_stock_class, nullptr,
                        limit, 16);
}

void
tcp_stock_get(StockMap &tcp_stock, struct pool &pool, const char *name,
              bool ip_transparent,
              SocketAddress bind_address,
              SocketAddress address,
              unsigned timeout,
              StockGetHandler &handler,
              CancellablePointer &cancel_ptr)
{
    assert(!address.IsNull());

    auto request = NewFromPool<TcpStockRequest>(pool, ip_transparent,
                                                bind_address, address,
                                                timeout);

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

    tcp_stock.Get(pool, name, request, handler, cancel_ptr);
}

SocketDescriptor
tcp_stock_item_get(const StockItem &item)
{
    auto *connection = (const TcpStockConnection *)&item;

    return connection->fd;
}

SocketAddress
tcp_stock_item_get_address(const StockItem &item)
{
    auto &connection = (const TcpStockConnection &)item;

    assert(connection.fd.IsDefined());

    return connection.address;
}

int
tcp_stock_item_get_domain(const StockItem &item)
{
    auto *connection = (const TcpStockConnection *)&item;

    assert(connection->fd.IsDefined());

    return connection->address.GetFamily();
}
