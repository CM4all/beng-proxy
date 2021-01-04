/*
 * Copyright 2007-2021 CM4all GmbH
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
	auto host = eh.host != nullptr
		? eh.host
		: _host;

	static constexpr char a = '[';
	static constexpr char b = ']';
	const size_t is_ipv6 = eh.host != nullptr && eh.host.Find(':') != nullptr;
	const size_t need_brackets = is_ipv6 && port_length > 0;

	return alloc.Concat("https://",
			    StringView(&a, need_brackets),
			    host,
			    StringView(&b, need_brackets),
			    StringView(port_buffer, port_length),
			    uri);
}
