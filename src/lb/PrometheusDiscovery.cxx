// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PrometheusDiscovery.hxx"
#include "PrometheusDiscoveryConfig.hxx"
#include "Context.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Status.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/istream_gb.hxx"
#include "lib/avahi/Explorer.hxx"
#include "net/InetAddress.hxx"
#include "net/FormatAddress.hxx"

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
		if (!ToString(buffer, address))
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
					const InetAddress &address,
					[[maybe_unused]] AvahiStringList *txt,
					[[maybe_unused]] Flags flags) noexcept
{
	members.insert_or_assign(key, address);
}

void
LbPrometheusDiscovery::OnAvahiRemoveObject(const std::string &key) noexcept
{
	members.erase(key);
}
