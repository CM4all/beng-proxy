// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HttpStats.hxx"
#include "stats/HttpStats.hxx"
#include "stats/TaggedHttpStats.hxx"
#include "memory/GrowingBuffer.hxx"

#include <inttypes.h>
#include <stdio.h>

namespace Prometheus {

static void
Write(GrowingBuffer &buffer, const char *labels,
      const HttpStats &stats) noexcept
{
	buffer.Format(
	       R"(
# HELP beng_proxy_http_requests Number of HTTP requests
# TYPE beng_proxy_http_requests counter

# HELP beng_proxy_http_total_duration Total duration of all HTTP requests
# TYPE beng_proxy_http_total_duration counter

# HELP beng_proxy_http_traffic Number of bytes transferred
# TYPE beng_proxy_http_traffic counter

)"
	       "beng_proxy_http_total_duration{%s} %e\n"
	       "beng_proxy_http_traffic{%sdirection=\"in\"} %" PRIu64 "\n"
	       "beng_proxy_http_traffic{%sdirection=\"out\"} %" PRIu64 "\n",
	       labels, std::chrono::duration_cast<std::chrono::duration<double>>(stats.total_duration).count(),
	       labels, stats.traffic_received,
	       labels, stats.traffic_sent);

	for (std::size_t i = 0; i < stats.n_per_status.size(); ++i)
		if (stats.n_per_status[i] > 0)
			buffer.Format("beng_proxy_http_requests{%sstatus=\"%u\"} %" PRIu64 "\n",
				      labels,
				      static_cast<unsigned>(IndexToHttpStatus(i)),
				      stats.n_per_status[i]);
}

void
Write(GrowingBuffer &buffer, const char *process, const char *listener,
      const HttpStats &stats) noexcept
{
	char labels[256];
	snprintf(labels, sizeof(labels),
		 "process=\"%s\",listener=\"%s\",",
		 process, listener);

	Write(buffer, labels, stats);
}

void
Write(GrowingBuffer &buffer, const char *process, const char *listener,
      const TaggedHttpStats &tagged_stats) noexcept
{
	for (const auto &[tag, stats] : tagged_stats.per_tag) {
		char labels[256];
		snprintf(labels, sizeof(labels),
			 "process=\"%s\",listener=\"%s\",tag=\"%s\",",
			 process, listener, tag.c_str());

		Write(buffer, labels, stats);
	}
}

} // namespace Prometheus
