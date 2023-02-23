// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RedirectHttps.hxx"
#include "net/HostParser.hxx"
#include "AllocatorPtr.hxx"

#include <stdio.h>

const char *
MakeHttpsRedirect(AllocatorPtr alloc, const char *_host, uint16_t port,
		  const char *uri) noexcept
{
	char port_buffer[16];
	size_t port_length = 0;
	if (port != 0 && port != 443)
		port_length = sprintf(port_buffer, ":%u", port);

	auto eh = ExtractHost(_host);
	auto host = eh.host.data() != nullptr
		? eh.host
		: _host;

	static constexpr char a = '[';
	static constexpr char b = ']';
	const size_t is_ipv6 =
		std::find(eh.host.begin(), eh.host.end(), ':') != eh.host.end();
	const size_t need_brackets = is_ipv6 && port_length > 0;

	return alloc.Concat("https://",
			    std::string_view{&a, need_brackets},
			    host,
			    std::string_view{&b, need_brackets},
			    std::string_view{port_buffer, port_length},
			    uri);
}
