// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HttpStats.hxx"
#include "stats/HttpStats.hxx"
#include "stats/TaggedHttpStats.hxx"
#include "memory/GrowingBuffer.hxx"
#include "lib/fmt/ToBuffer.hxx"

namespace Prometheus {

static void
Write(GrowingBuffer &buffer, std::string_view labels,
      const HttpStats &stats) noexcept
{
	buffer.Fmt(
	       R"(
# HELP beng_proxy_http_requests Number of HTTP requests
# TYPE beng_proxy_http_requests counter

# HELP beng_proxy_http_requests_delayed Number of delayed HTTP requests
# TYPE beng_proxy_http_requests_delayed counter

# HELP beng_proxy_http_total_duration Total duration of all HTTP requests
# TYPE beng_proxy_http_total_duration counter

# HELP beng_proxy_http_traffic Number of bytes transferred
# TYPE beng_proxy_http_traffic counter

)"
	       "beng_proxy_http_requests_delayed{{{}}} {}\n"
	       "beng_proxy_http_total_duration{{{}}} {:e}\n"
	       "beng_proxy_http_traffic{{{}direction=\"in\"}} {}\n"
	       "beng_proxy_http_traffic{{{}direction=\"out\"}} {}\n",
	       labels, stats.n_delayed,
	       labels, std::chrono::duration_cast<std::chrono::duration<double>>(stats.total_duration).count(),
	       labels, stats.traffic_received,
	       labels, stats.traffic_sent);

	for (std::size_t i = 0; i < stats.n_per_status.size(); ++i)
		if (stats.n_per_status[i] > 0)
			buffer.Fmt("beng_proxy_http_requests{{{}status=\"{}\"}} {}\n",
				   labels,
				   static_cast<unsigned>(IndexToHttpStatus(i)),
				   stats.n_per_status[i]);
}

void
Write(GrowingBuffer &buffer, std::string_view process, std::string_view listener,
      const HttpStats &stats) noexcept
{
	const auto labels = FmtBuffer<256>("process=\"{}\",listener=\"{}\",",
					   process, listener);

	Write(buffer, labels.c_str(), stats);
}

void
Write(GrowingBuffer &buffer, std::string_view process, std::string_view listener,
      const TaggedHttpStats &tagged_stats) noexcept
{
	for (const auto &[tag, stats] : tagged_stats.per_tag) {
		const auto labels = FmtBuffer<256>("process=\"{}\",listener=\"{}\",tag=\"{}\",",
						   process, listener, tag);

		Write(buffer, labels.c_str(), stats);
	}
}

} // namespace Prometheus
