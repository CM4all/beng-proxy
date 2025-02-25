// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SConnection.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <errno.h>
#include <string.h>

FcgiStockConnection::FcgiStockConnection(CreateStockItem c, ListenChildStockItem &_child,
					 UniqueSocketDescriptor &&socket) noexcept
	:StockItem(c), logger(GetStockName()),
	 child(_child),
	 event(GetStock().GetEventLoop(), BIND_THIS_METHOD(OnSocketEvent),
	       socket.Release()),
	 defer_schedule_read(GetStock().GetEventLoop(),
			     BIND_THIS_METHOD(DeferredScheduleRead))
{
}

void
FcgiStockConnection::SetAborted() noexcept
{
	if (fresh)
		child.Fade();
}

inline void
FcgiStockConnection::Read() noexcept
{
	std::byte buffer[1];
	ssize_t nbytes = GetSocket().ReadNoWait(buffer);
	if (nbytes < 0)
		logger(2, "error on idle FastCGI connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle FastCGI connection");
}

inline void
FcgiStockConnection::OnSocketEvent(unsigned) noexcept
{
	Read();
	InvokeIdleDisconnect();
}

bool
FcgiStockConnection::Borrow() noexcept
{
	if (event.GetReadyFlags() != 0) [[unlikely]] {
		/* this connection was probably closed, but our
		   SocketEvent callback hasn't been invoked yet;
		   refuse to use this item; the caller will destroy
		   the connection */
		Read();
		return false;
	}

	event.Cancel();
	defer_schedule_read.Cancel();
	return true;
}

bool
FcgiStockConnection::Release() noexcept
{
	fresh = false;
	defer_schedule_read.ScheduleIdle();
	return true;
}

FcgiStockConnection::~FcgiStockConnection() noexcept
{
	event.Close();
}
