/*
 * Copyright 2007-2019 CM4all GmbH
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
#include "AllocatorPtr.hxx"
#include "pool/DisposablePointer.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "stock/LoggerDomain.hxx"
#include "address_list.hxx"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"
#include "net/PConnectSocket.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/ToString.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"
#include "stopwatch.hxx"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct TcpStockRequest {
	AllocatorPtr alloc;

	StopwatchPtr stopwatch;

	const bool ip_transparent;

	const SocketAddress bind_address, address;

	const Event::Duration timeout;

	TcpStockRequest(AllocatorPtr _alloc, const StopwatchPtr &parent_stopwatch,
			const char *name,
			bool _ip_transparent, SocketAddress _bind_address,
			SocketAddress _address, Event::Duration _timeout) noexcept
		:alloc(_alloc),
		 stopwatch(parent_stopwatch, name),
		 ip_transparent(_ip_transparent), bind_address(_bind_address),
		 address(_address), timeout(_timeout) {}
};

struct TcpStockConnection final
	: StockItem, ConnectSocketHandler, Cancellable {

	BasicLogger<StockLoggerDomain> logger;

	/**
	 * To cancel the ClientSocket.
	 */
	CancellablePointer cancel_ptr;

	SocketDescriptor fd = SocketDescriptor::Undefined();

	const AllocatedSocketAddress address;

	SocketEvent event;
	TimerEvent idle_timeout_event;

	TcpStockConnection(CreateStockItem c, SocketAddress _address,
			   CancellablePointer &_cancel_ptr) noexcept
		:StockItem(c),
		 logger(c.stock),
		 address(_address),
		 event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)),
		 idle_timeout_event(c.stock.GetEventLoop(),
				    BIND_THIS_METHOD(OnIdleTimeout))
	{
		_cancel_ptr = *this;

		cancel_ptr = nullptr;
	}

	~TcpStockConnection() noexcept override;

private:
	void EventCallback(unsigned events) noexcept;
	void OnIdleTimeout() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(cancel_ptr);

		cancel_ptr.CancelAndClear();
		InvokeCreateAborted();
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		event.Cancel();
		idle_timeout_event.Cancel();
		return true;
	}

	bool Release() noexcept override {
		event.ScheduleRead();
		idle_timeout_event.Schedule(std::chrono::minutes(1));
		return true;
	}
};


/*
 * libevent callback
 *
 */

inline void
TcpStockConnection::EventCallback(unsigned) noexcept
{
	char buffer;
	ssize_t nbytes;

	nbytes = fd.Read(&buffer, sizeof(buffer));
	if (nbytes < 0)
		logger(2, "error on idle TCP connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data in idle TCP connection");

	InvokeIdleDisconnect();
}

inline void
TcpStockConnection::OnIdleTimeout() noexcept
{
	InvokeIdleDisconnect();
}


/*
 * client_socket callback
 *
 */

void
TcpStockConnection::OnSocketConnectSuccess(UniqueSocketDescriptor &&new_fd) noexcept
{
	cancel_ptr = nullptr;

	fd = new_fd.Release();
	event.Open(fd);

	InvokeCreateSuccess();
}

void
TcpStockConnection::OnSocketConnectError(std::exception_ptr ep) noexcept
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
TcpStock::Create(CreateStockItem c,
		 StockRequest _request,
		 CancellablePointer &cancel_ptr)
{
	TcpStockRequest *request = (TcpStockRequest *)_request.get();

	auto *connection = new TcpStockConnection(c,
						  request->address,
						  cancel_ptr);

	client_socket_new(c.stock.GetEventLoop(), request->alloc,
			  std::move(request->stopwatch),
			  request->address.GetFamily(), SOCK_STREAM, 0,
			  request->ip_transparent,
			  request->bind_address,
			  request->address,
			  request->timeout,
			  *connection,
			  connection->cancel_ptr);
}

TcpStockConnection::~TcpStockConnection() noexcept
{
	if (cancel_ptr)
		cancel_ptr.Cancel();
	else if (fd.IsDefined()) {
		event.Cancel();
		fd.Close();
	}
}

/*
 * interface
 *
 */

void
TcpStock::Get(AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
	      const char *name,
	      bool ip_transparent,
	      SocketAddress bind_address,
	      SocketAddress address,
	      Event::Duration timeout,
	      StockGetHandler &handler,
	      CancellablePointer &cancel_ptr)
{
	assert(!address.IsNull());

	if (name == nullptr) {
		char buffer[1024];
		if (!ToString(buffer, sizeof(buffer), address))
			buffer[0] = 0;

		if (!bind_address.IsNull()) {
			char bind_buffer[1024];
			if (!ToString(bind_buffer, sizeof(bind_buffer), bind_address))
				bind_buffer[0] = 0;
			name = alloc.Concat(bind_buffer, ">", buffer);
		} else
			name = alloc.Dup(buffer);
	}

	auto request = NewDisposablePointer<TcpStockRequest>(alloc, alloc,
							     parent_stopwatch,
							     name,
							     ip_transparent,
							     bind_address, address,
							     timeout);

	stock.Get(name, std::move(request), handler, cancel_ptr);
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
