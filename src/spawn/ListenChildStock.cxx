// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ListenChildStock.hxx"
#include "net/UniqueSocketDescriptor.hxx"

int
ListenChildStockClass::GetChildSocketType(void *) const noexcept
{
	return SOCK_STREAM;
}

std::unique_ptr<ChildStockItem>
ListenChildStockClass::CreateChild(CreateStockItem c,
				   void *info,
				   ChildStock &child_stock)
{
	return std::make_unique<ListenChildStockItem>(c, child_stock,
						      GetChildTag(info));
}

void
ListenChildStockItem::Prepare(ChildStockClass &_cls, void *info,
			      PreparedChildProcess &p,
			      FdHolder &close_fds)
{
	auto &cls = (ListenChildStockClass &)_cls;

	ChildStockItem::Prepare(cls, info, p, close_fds);

	const int socket_type = cls.GetChildSocketType(info);
	const unsigned backlog = cls.GetChildBacklog(info);

	cls.PrepareListenChild(info, socket.Create(socket_type, backlog),
			       p, close_fds);
}

UniqueSocketDescriptor
ListenChildStockItem::Connect()
{
	try {
		return socket.Connect();
	} catch (...) {
		/* if the connection fails, abandon the child process, don't
		   try again - it will never work! */
		Fade();
		throw;
	}
}
