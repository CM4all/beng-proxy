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

#include "PrometheusDiscovery.hxx"
#include "PrometheusDiscoveryConfig.hxx"
#include "Context.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Status.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/istream_gb.hxx"
#include "lib/avahi/Explorer.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/ToString.hxx"

using std::string_view_literals::operator""sv;

LbPrometheusDiscovery::LbPrometheusDiscovery(const LbPrometheusDiscoveryConfig &config,
					     const LbContext &context) noexcept
	:explorer(config.zeroconf.Create(context.GetAvahiClient(),
					 *this, context.avahi_error_handler))

{
}

LbPrometheusDiscovery::~LbPrometheusDiscovery() noexcept = default;

GrowingBuffer
LbPrometheusDiscovery::GenerateJSON() const noexcept
{
	GrowingBuffer b;
	b.Write("[{\"targets\":["sv);

	bool first = true;
	for (const auto &[_, address] : members) {
		char buffer[256];
		if (!ToString(buffer, sizeof(buffer), address))
			continue;

		if (first)
			first = false;
		else
			b.Write(","sv);

		b.Write("\""sv);
		b.Write(buffer);
		b.Write("\""sv);
	}

	b.Write("], \"labels\":{}}]");
	return b;
}

void
LbPrometheusDiscovery::HandleHttpRequest(IncomingHttpRequest &request,
					 const StopwatchPtr &,
					 CancellablePointer &) noexcept
{
	HttpHeaders headers;
	headers.Write("content-type", "application/json");

	request.SendResponse(HttpStatus::OK, std::move(headers),
			     istream_gb_new(request.pool, GenerateJSON()));
}

void
LbPrometheusDiscovery::OnAvahiNewObject(const std::string &key,
					SocketAddress address) noexcept
{
	members.insert_or_assign(key, address);
}

void
LbPrometheusDiscovery::OnAvahiRemoveObject(const std::string &key) noexcept
{
	members.erase(key);
}
