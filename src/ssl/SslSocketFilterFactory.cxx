// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SslSocketFilterFactory.hxx"
#include "Client.hxx"

SocketFilterPtr
SslSocketFilterFactory::CreateFilter()
{
	return ssl_client_factory.Create(event_loop,
					 host.empty() ? nullptr : host.c_str(),
					 certificate.empty() ? nullptr : certificate.c_str(),
					 alpn);
}

SocketFilterFactoryPtr
SslSocketFilterParams::CreateFactory() const noexcept
{
	return std::make_unique<SslSocketFilterFactory>(event_loop, ssl_client_factory,
							host, certificate, alpn);
}
