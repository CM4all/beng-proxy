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

#include "Stats.hxx"
#include "net/control/Protocol.hxx"
#include "util/ByteOrder.hxx"
#include "memory/GrowingBuffer.hxx"

#include <inttypes.h>

namespace Prometheus {

void
Write(GrowingBuffer &buffer, const char *process,
      const BengProxy::ControlStats &stats) noexcept
{
	buffer.Format(
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
	       "beng_proxy_connections{process=\"%s\",direction=\"in\"} %" PRIu32 "\n"
	       "beng_proxy_connections{process=\"%s\",direction=\"out\"} %" PRIu32 "\n"
	       "beng_proxy_children{process=\"%s\"} %" PRIu32 "\n"
	       "beng_proxy_sessions{process=\"%s\"} %" PRIu32 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"translation\",metric=\"netto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"translation\",metric=\"brutto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"http\",metric=\"netto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"http\",metric=\"brutto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"filter\",metric=\"netto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"filter\",metric=\"brutto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"nfs\",metric=\"netto\"} %" PRIu64 "\n"
	       "beng_proxy_cache_size{process=\"%s\",type=\"nfs\",metric=\"brutto\"} %" PRIu64 "\n"
	       "beng_proxy_buffer_size{process=\"%s\",type=\"io\",metric=\"netto\"} %" PRIu64 "\n"
	       "beng_proxy_buffer_size{process=\"%s\",type=\"io\",metric=\"brutto\"} %" PRIu64 "\n",
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
