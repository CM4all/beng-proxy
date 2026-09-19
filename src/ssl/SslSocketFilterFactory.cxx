// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SslSocketFilterFactory.hxx"
#include "Client.hxx"
#include "util/StringBuilder.hxx"

void
SslSocketFilterParams::AppendFilterId(StringBuilder &b) const
{
	if (host != nullptr)
		b.Append(std::string_view{host});

	/* the client certificate and the ALPN setting determine the
	   identity of the TLS connection, and therefore they need to
	   be part of the identifier; without them, a connection which
	   was authenticated with one certificate could be reused for
	   a request which specifies a different certificate */

	if (certificate != nullptr) {
		b.Append('/');
		b.Append(std::string_view{certificate});
	}

	using std::string_view_literals::operator""sv;

	switch (alpn) {
	case SslClientAlpn::NONE:
		break;

	case SslClientAlpn::HTTP_2:
		b.Append("|h2"sv);
		break;

	case SslClientAlpn::HTTP_ANY:
		b.Append("|h*"sv);
		break;
	}
}

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
