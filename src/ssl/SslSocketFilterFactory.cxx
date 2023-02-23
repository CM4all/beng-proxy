// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SslSocketFilterFactory.hxx"
#include "Client.hxx"

SocketFilterPtr
SslSocketFilterFactory::CreateFilter()
{
	return ssl_client_factory.Create(event_loop, host, certificate, alpn);
}
