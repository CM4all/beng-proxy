/*
 * Copyright 2007-2022 CM4all GmbH
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
			      PreparedChildProcess &p)
{
	auto &cls = (ListenChildStockClass &)_cls;

	ChildStockItem::Prepare(cls, info, p);

	const int socket_type = cls.GetChildSocketType(info);
	const unsigned backlog = cls.GetChildBacklog(info);

	cls.PrepareListenChild(info, socket.Create(socket_type, backlog), p);
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
