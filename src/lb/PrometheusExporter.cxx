/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "PrometheusExporter.hxx"
#include "Instance.hxx"
#include "beng-proxy/Control.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "util/ByteOrder.hxx"
#include "util/Compiler.h"
#include "GrowingBuffer.hxx"
#include "istream_gb.hxx"

#include <inttypes.h>
#include <stdarg.h>

gcc_printf(2, 3)
static void
Format(GrowingBuffer &buffer, const char *fmt, ...) noexcept
{
	va_list ap;
	va_start(ap, fmt);
	const auto reserve_size = (std::size_t)
		vsnprintf(nullptr, 0, fmt, ap) + 1;
	va_end(ap);

	char *p = (char *)buffer.BeginWrite(reserve_size);

	va_start(ap, fmt);
	const auto length = (std::size_t)
		vsnprintf(p, reserve_size, fmt, ap);
	va_end(ap);

	assert(length + 1 == reserve_size);

	buffer.CommitWrite(length);
}

static void
Write(GrowingBuffer &buffer, const char *process, const BengProxy::ControlStats &stats) noexcept
{
	Format(buffer,
	       "beng_proxy_connections{process=\"%s\",direction=\"in\"} %" PRIu32 "\n"
	       "beng_proxy_connections{process=\"%s\",direction=\"out\"} %" PRIu32 "\n"
	       "beng_proxy_children{process=\"%s\"} %" PRIu32 "\n"
	       "beng_proxy_sessions{process=\"%s\"} %" PRIu32 "\n"
	       "beng_proxy_http_requests{process=\"%s\"} %" PRIu64 "\n"
	       "beng_proxy_http_traffic{process=\"%s\",direction=\"in\"} %" PRIu64 "\n"
	       "beng_proxy_http_traffic{process=\"%s\",direction=\"out\"} %" PRIu64 "\n"
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
	       process, FromBE64(stats.http_requests),
	       process, FromBE64(stats.http_traffic_received),
	       process, FromBE64(stats.http_traffic_sent),
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

void
LbPrometheusExporter::HandleRequest(IncomingHttpRequest &request,
				    CancellablePointer &) noexcept
{
	GrowingBuffer buffer;

	const char *process = "lb";
	if (instance != nullptr)
		Write(buffer, process, instance->GetStats());

	request.SendResponse(HTTP_STATUS_OK, {},
			     istream_gb_new(request.pool, std::move(buffer)));
}
