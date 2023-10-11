// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stats.hxx"
#include "net/control/Protocol.hxx"
#include "util/ByteOrder.hxx"
#include "memory/GrowingBuffer.hxx"

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      const BengProxy::ControlStats &stats) noexcept
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

)"
	       "beng_proxy_connections{{process=\"{}\",direction=\"in\"}} {}\n"
	       "beng_proxy_connections{{process=\"{}\",direction=\"out\"}} {}\n"
	       "beng_proxy_children{{process=\"{}\"}} {}\n"
	       "beng_proxy_sessions{{process=\"{}\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"translation\",metric=\"netto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"translation\",metric=\"brutto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"http\",metric=\"netto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"http\",metric=\"brutto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"filter\",metric=\"netto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"filter\",metric=\"brutto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"nfs\",metric=\"netto\"}} {}\n"
	       "beng_proxy_cache_size{{process=\"{}\",type=\"nfs\",metric=\"brutto\"}} {}\n"
	       "beng_proxy_buffer_size{{process=\"{}\",type=\"io\",metric=\"netto\"}} {}\n"
	       "beng_proxy_buffer_size{{process=\"{}\",type=\"io\",metric=\"brutto\"}} {}\n",
	       process, FromBE32(stats.incoming_connections),
	       process, FromBE32(stats.outgoing_connections),
	       process, FromBE32(stats.children),
	       process, FromBE32(stats.sessions),
	       process, FromBE64(stats.translation_cache_size),
	       process, FromBE64(stats.translation_cache_brutto_size),
	       process, FromBE64(stats.http_cache_size),
	       process, FromBE64(stats.http_cache_brutto_size),
	       process, FromBE64(stats.filter_cache_size),
	       process, FromBE64(stats.filter_cache_brutto_size),
	       process, FromBE64(stats.nfs_cache_size),
	       process, FromBE64(stats.nfs_cache_brutto_size),
	       process, FromBE64(stats.io_buffers_size),
	       process, FromBE64(stats.io_buffers_brutto_size));
}

} // namespace Prometheus
