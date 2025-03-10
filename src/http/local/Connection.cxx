// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Connection.hxx"
#include "spawn/ListenChildStock.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Compiler.h" // for gcc_unreachable()

LhttpConnection::LhttpConnection(CreateStockItem c,
				 ListenChildStockItem &_child)
	:StockItem(c),
	 logger(GetStockName()),
	 child(_child),
	 socket(c.stock.GetEventLoop(), _child.Connect(), FdType::FD_SOCKET)
{
}

LhttpConnection::~LhttpConnection() noexcept = default;

std::string_view
LhttpConnection::GetTag() const noexcept
{
	return child.GetTag();
}

void
LhttpConnection::SetSite(const char *site) noexcept
{
	child.SetSite(site);
}

void
LhttpConnection::SetUri(const char *uri) noexcept
{
	child.SetUri(uri);
}

BufferedResult
LhttpConnection::OnBufferedData()
{
	logger(2, "unexpected data in idle LHTTP connection");
	InvokeIdleDisconnect();
	return BufferedResult::DESTROYED;
}

bool
LhttpConnection::OnBufferedHangup() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
LhttpConnection::OnBufferedClosed() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
LhttpConnection::OnBufferedWrite()
{
	/* should never be reached because we never schedule
	   writing */
	assert(false);
	gcc_unreachable();
}

void
LhttpConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	logger(2, "error on idle LHTTP connection: ", e);
	InvokeIdleDisconnect();
}

bool
LhttpConnection::Borrow() noexcept
{
	return true;
}

bool
LhttpConnection::Release() noexcept
{
	assert(socket.IsValid());
	assert(socket.IsConnected());

	if (!socket.IsEmpty()) {
		logger(2, "unexpected data in idle LHTTP connection");
		return false;
	}

	socket.Reinit(Event::Duration(-1), *this);
	socket.UnscheduleWrite();

	socket.ScheduleRead();
	return true;
}
