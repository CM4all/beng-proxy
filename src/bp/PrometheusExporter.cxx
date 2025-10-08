// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PrometheusExporter.hxx"
#include "Instance.hxx"
#include "LStats.hxx"
#include "prometheus/Stats.hxx"
#include "prometheus/HttpStats.hxx"
#include "prometheus/SpawnStats.hxx"
#include "prometheus/StockStats.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "stats/TaggedHttpStats.hxx"
#include "spawn/Client.hxx"
#include "http/local/Stock.hxx"
#include "fcgi/Stock.hxx"
#include "fs/Stock.hxx"
#include "was/Stock.hxx"
#include "was/MStock.hxx"
#include "was/RStock.hxx"
#include "stock/Stats.hxx"
#include "memory/istream_gb.hxx"
#include "memory/GrowingBuffer.hxx"
#include "tcp_stock.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

static void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view listener,
      const BpListenerStats &stats) noexcept
{
	Prometheus::Write(buffer, process, listener, stats.tagged);
	Prometheus::Write(buffer, process, listener, stats.per_generator);
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

	if (instance.spawn)
		Prometheus::Write(buffer, process, instance.spawn->GetStats());

	for (const auto &[name, stats] : instance.listener_stats)
		Prometheus::Write(buffer, process, name, stats);

	if (instance.tcp_stock != nullptr || instance.fs_stock) {
		StockStats stats{};

		if (instance.tcp_stock != nullptr)
			instance.tcp_stock->AddStats(stats);

		if (instance.fs_stock != nullptr)
			instance.fs_stock->AddStats(stats);

		Prometheus::Write(buffer, process, "tcp"sv, stats);
	}

	if (instance.lhttp_stock) {
		StockStats stats{};
		instance.lhttp_stock->AddStats(stats);
		Prometheus::Write(buffer, process, "lhttp"sv, stats);
	}

	if (instance.fcgi_stock) {
		StockStats stats{};
		instance.fcgi_stock->AddStats(stats);
		Prometheus::Write(buffer, process, "fcgi"sv, stats);
	}

	if (instance.was_stock || instance.multi_was_stock || instance.remote_was_stock) {
		StockStats stats{};
		if (instance.was_stock)
			instance.was_stock->AddStats(stats);
		if (instance.multi_was_stock)
			instance.multi_was_stock->AddStats(stats);
		if (instance.remote_was_stock)
			instance.remote_was_stock->AddStats(stats);
		Prometheus::Write(buffer, process, "was"sv, stats);
	}

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
