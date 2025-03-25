// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SConnection.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/FdType.hxx"

#include <utility> // for std::unreachable()

FcgiStockConnection::FcgiStockConnection(CreateStockItem c, ListenChildStockItem &_child,
					 UniqueSocketDescriptor &&_socket) noexcept
	:StockItem(c),
	 logger(GetStockNameView()),
	 child(_child),
	 socket(GetStock().GetEventLoop())
{
	socket.Init(_socket.Release(), FdType::FD_SOCKET, Event::Duration{-1}, *this);
	socket.ScheduleRead();
}

FcgiStockConnection::~FcgiStockConnection() noexcept = default;

void
FcgiStockConnection::SetAborted() noexcept
{
	if (fresh)
		child.Fade();
}

BufferedResult
FcgiStockConnection::OnBufferedData()
{
	logger(2, "unexpected data from idle FastCGI connection");
	InvokeIdleDisconnect();
	return BufferedResult::DESTROYED;
}

bool
FcgiStockConnection::OnBufferedHangup() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
FcgiStockConnection::OnBufferedClosed() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
FcgiStockConnection::OnBufferedWrite()
{
	/* should never be reached because we never schedule
	   writing */
	std::unreachable();
}

void
FcgiStockConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	logger(2, "error on idle FastCGI connection: ", std::move(e));
	InvokeIdleDisconnect();
}

bool
FcgiStockConnection::Borrow() noexcept
{
	if (socket.GetReadyFlags() != 0) [[unlikely]] {
		/* this connection was probably closed, but our
		   SocketEvent callback hasn't been invoked yet;
		   refuse to use this item; the caller will destroy
		   the connection */
		return false;
	}

	return true;
}

bool
FcgiStockConnection::Release() noexcept
{
	fresh = false;

	socket.Reinit(Event::Duration(-1), *this);
	socket.UnscheduleWrite();

	socket.ScheduleRead();
	return true;
}
