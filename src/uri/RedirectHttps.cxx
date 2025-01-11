// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RedirectHttps.hxx"
#include "lib/fmt/Unsafe.hxx"
#include "net/HostParser.hxx"
#include "AllocatorPtr.hxx"

const char *
MakeHttpsRedirect(AllocatorPtr alloc, const char *_host, uint16_t port,
		  const char *uri) noexcept
{
	char port_buffer[16];
	std::string_view port_sv{};
	if (port != 0 && port != 443)
		port_sv = FmtUnsafeSV(port_buffer, ":{}", port);

	auto eh = ExtractHost(_host);
	auto host = eh.host.data() != nullptr
		? eh.host
		: _host;

	static constexpr char a = '[';
	static constexpr char b = ']';
	const size_t is_ipv6 =
		std::find(eh.host.begin(), eh.host.end(), ':') != eh.host.end();
	const size_t need_brackets = is_ipv6 && !port_sv.empty();

	return alloc.Concat("https://",
			    std::string_view{&a, need_brackets},
			    host,
			    std::string_view{&b, need_brackets},
			    port_sv,
			    uri);
}
