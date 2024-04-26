// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stats.hxx"
#include "net/control/Protocol.hxx"
#include "memory/GrowingBuffer.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

static void
Write(GrowingBuffer &buffer, std::string_view metric_name,
      std::string_view process, std::string_view type,
      const AllocatorStats &stats) noexcept
{
	buffer.Fmt(R"(
{}{{process={:?},type={:?},metric="netto"}} {}
{}{{process={:?},type={:?},metric="brutto"}} {}
)",
		   metric_name, process, type, stats.netto_size,
		   metric_name, process, type, stats.brutto_size);
}

static void
Write(GrowingBuffer &buffer,
      std::string_view process, std::string_view type,
      const CacheStats &stats) noexcept
{
	Write(buffer, "beng_proxy_cache_size"sv, process, type, stats.allocator);
}

void
Write(GrowingBuffer &buffer, std::string_view process,
      const Stats &stats) noexcept
{
	buffer.Fmt(
	       R"(
# HELP beng_proxy_connections Number of connections
# TYPE beng_proxy_connections gauge

# HELP beng_proxy_children Number of child processes
# TYPE beng_proxy_children gauge

# HELP beng_proxy_sessions Number of sessions
# TYPE beng_proxy_sessions gauge

# HELP beng_proxy_cache_size Size of the cache in bytes
# TYPE beng_proxy_cache_size gauge

# HELP beng_proxy_buffer_size Size of buffers in bytes
# TYPE beng_proxy_buffer_size gauge

beng_proxy_connections{{process={:?},direction="in"}} {}
beng_proxy_connections{{process={:?},direction="out"}} {}
beng_proxy_children{{process={:?}}} {}
beng_proxy_sessions{{process={:?}}} {}
)",
	       process, stats.incoming_connections,
	       process, stats.outgoing_connections,
	       process, stats.children,
	       process, stats.sessions);

	Write(buffer, process, "translation"sv, stats.translation_cache);
	Write(buffer, process, "http"sv, stats.http_cache);
	Write(buffer, process, "filter"sv, stats.filter_cache);
	Write(buffer, "beng_proxy_buffer_size"sv, process, "io"sv, stats.io_buffers);
}

} // namespace Prometheus
