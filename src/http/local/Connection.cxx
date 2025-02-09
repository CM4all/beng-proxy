// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Connection.hxx"
#include "spawn/ListenChildStock.hxx"
#include "net/UniqueSocketDescriptor.hxx"

LhttpConnection::LhttpConnection(CreateStockItem c,
				 ListenChildStockItem &_child)
	:StockItem(c),
	 logger(*this),
	 child(_child),
	 event(c.stock.GetEventLoop(),
	       BIND_THIS_METHOD(EventCallback),
	       _child.Connect().Release())
{
}

LhttpConnection::~LhttpConnection() noexcept
{
	event.Close();
}

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

inline void
LhttpConnection::Read() noexcept
{
	std::byte buffer[1];
	ssize_t nbytes = GetSocket().ReadNoWait(buffer);
	if (nbytes < 0)
		logger(2, "error on idle LHTTP connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle LHTTP connection");
}

inline void
LhttpConnection::EventCallback(unsigned) noexcept
{
	Read();
	InvokeIdleDisconnect();
}

bool
LhttpConnection::Borrow() noexcept
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
	return true;
}

bool
LhttpConnection::Release() noexcept
{
	event.ScheduleRead();
	return true;
}
