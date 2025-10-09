// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "HttpStats.hxx"
#include "stats/HttpStats.hxx"
#include "stats/TaggedHttpStats.hxx"
#include "stats/PerGeneratorStats.hxx"
#include "memory/GrowingBuffer.hxx"
#include "lib/fmt/ToBuffer.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

static void
Write(GrowingBuffer &buffer,
      std::string_view name, std::string_view labels,
      const PerHttpStatusCounters &per_status) noexcept
{
	for (std::size_t i = 0; i < per_status.size(); ++i)
		if (per_status[i] > 0)
			buffer.Fmt("{}{{{}status=\"{}\"}} {}\n",
				   name, labels,
				   static_cast<unsigned>(IndexToHttpStatus(i)),
				   per_status[i]);
}

static void
Write(GrowingBuffer &buffer, std::string_view labels,
      const HttpStats &stats) noexcept
{
	buffer.Fmt(
	       R"(
# HELP beng_proxy_http_requests Number of HTTP requests
# TYPE beng_proxy_http_requests counter

# HELP beng_proxy_http_requests_rejected Number of rejected HTTP requests
# TYPE beng_proxy_http_requests_rejected counter

# HELP beng_proxy_http_requests_delayed Number of delayed HTTP requests
# TYPE beng_proxy_http_requests_delayed counter

# HELP beng_proxy_http_invalid_frames Number of invalid HTTP/2 frames
# TYPE beng_proxy_http_invalid_frames counter

# HELP beng_proxy_http_total_duration Total duration of all HTTP requests
# TYPE beng_proxy_http_total_duration counter

# HELP beng_proxy_http_traffic Number of bytes transferred
# TYPE beng_proxy_http_traffic counter

beng_proxy_http_requests_rejected{{{}}} {}
beng_proxy_http_requests_delayed{{{}}} {}
beng_proxy_http_invalid_frames{{{}}} {}
beng_proxy_http_total_duration{{{}}} {:e}
beng_proxy_http_traffic{{{}direction="in"}} {}
beng_proxy_http_traffic{{{}direction="out"}} {}
)",
	       labels, stats.n_rejected,
	       labels, stats.n_delayed,
	       labels, stats.n_invalid_frames,
	       labels, std::chrono::duration_cast<std::chrono::duration<double>>(stats.total_duration).count(),
	       labels, stats.traffic_received,
	       labels, stats.traffic_sent);

	Write(buffer, "beng_proxy_http_requests"sv, labels, stats.n_per_status);
}

void
Write(GrowingBuffer &buffer, std::string_view process, std::string_view listener,
      const HttpStats &stats) noexcept
{
	const auto labels = FmtBuffer<256>("process={:?},listener={:?},",
					   process, listener);

	Write(buffer, labels.c_str(), stats);
}

void
Write(GrowingBuffer &buffer, std::string_view process, std::string_view listener,
      const TaggedHttpStats &tagged_stats) noexcept
{
	for (const auto &[tag, stats] : tagged_stats.per_tag) {
		const auto labels = FmtBuffer<256>("process={:?},listener={:?},tag={:?},",
						   process, listener, tag);

		Write(buffer, labels.c_str(), stats);
	}
}

static void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view listener, std::string_view generator,
      const PerGeneratorStats &stats) noexcept
{
	const auto labels = FmtBuffer<256>("process={:?},listener={:?},generator={:?},",
					   process, listener, generator);

	Write(buffer, "beng_proxy_http_requests_per_generator"sv,
	      labels.c_str(), stats.n_per_status);
}

void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view listener,
      const PerGeneratorStatsMap &per_generator) noexcept
{
	buffer.Write(R"(
# HELP beng_proxy_http_requests_per_generator Number of HTTP requests per GENERATOR
# TYPE beng_proxy_http_requests_per_generator counter
)"sv);

	for (const auto &[generator, stats] : per_generator.per_generator)
		Write(buffer, process, listener, generator, stats);
}

} // namespace Prometheus
