// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Connection.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <errno.h>
#include <string.h>

FcgiConnection::FcgiConnection(CreateStockItem c, ListenChildStockItem &_child,
			       UniqueSocketDescriptor &&socket) noexcept
	:StockItem(c), logger(GetStockName()),
	 child(_child),
	 event(GetStock().GetEventLoop(), BIND_THIS_METHOD(OnSocketEvent),
	       socket.Release())
{
}

inline void
FcgiConnection::Read() noexcept
{
	std::byte buffer[1];
	ssize_t nbytes = GetSocket().ReadNoWait(buffer);
	if (nbytes < 0)
		logger(2, "error on idle FastCGI connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle FastCGI connection");
}

inline void
FcgiConnection::OnSocketEvent(unsigned) noexcept
{
	Read();
	InvokeIdleDisconnect();
}

bool
FcgiConnection::Borrow() noexcept
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
	aborted = false;
	return true;
}

bool
FcgiConnection::Release() noexcept
{
	fresh = false;
	event.ScheduleRead();
	return true;
}

FcgiConnection::~FcgiConnection() noexcept
{
	event.Close();

	bool kill = false;

	if (fresh && aborted)
		/* the fcgi_client caller has aborted the request before the
		   first response on a fresh connection was received: better
		   kill the child process, it may be failing on us
		   completely */
		kill = true;

	child.Put(kill ? PutAction::DESTROY : PutAction::REUSE);
}

UniqueFileDescriptor
fcgi_stock_item_get_stderr(const StockItem &item) noexcept
{
	const auto &connection = (const FcgiConnection &)item;
	return connection.GetStderr();
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
fcgi_stock_item_get(const StockItem &item) noexcept
{
	const auto *connection = (const FcgiConnection *)&item;

	return connection->GetSocket();
}

void
fcgi_stock_aborted(StockItem &item) noexcept
{
	auto *connection = (FcgiConnection *)&item;

	connection->SetAborted();
}
