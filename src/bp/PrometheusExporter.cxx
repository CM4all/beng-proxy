// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PrometheusExporter.hxx"
#include "Instance.hxx"
#include "LStats.hxx"
#include "prometheus/Stats.hxx"
#include "prometheus/HttpStats.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "stats/TaggedHttpStats.hxx"
#include "memory/istream_gb.hxx"
#include "memory/GrowingBuffer.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

static void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view listener,
      const BpListenerStats &stats) noexcept
{
	Prometheus::Write(buffer, process, listener, stats.tagged);
}

} // namespace Prometheus

void
BpPrometheusExporter::HandleHttpRequest(IncomingHttpRequest &request,
					const StopwatchPtr &,
					CancellablePointer &) noexcept
{
	GrowingBuffer buffer;

	constexpr auto process = "bp"sv;
	Prometheus::Write(buffer, process, instance.GetStats());

	for (const auto &[name, stats] : instance.listener_stats)
		Prometheus::Write(buffer, process, name, stats);

#ifdef HAVE_LIBWAS
	buffer.Write("# HELP beng_proxy_was_metric Metric received from WAS applications\n"
		     "# TYPE beng_proxy_was_metric counter\n"sv);

	for (const auto &[name, value] : instance.was_metrics)
		buffer.Fmt("beng_proxy_was_metric{{name={:?}}} {:e}\n",
			   name, value);
#endif

	HttpHeaders headers;
	headers.Write("content-type", "text/plain;version=0.0.4");

	request.SendResponse(HttpStatus::OK, std::move(headers),
			     istream_gb_new(request.pool, std::move(buffer)));
}
