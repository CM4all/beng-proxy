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
#include "PrometheusExporterConfig.hxx"
#include "Instance.hxx"
#include "beng-proxy/Control.hxx"
#include "http/Address.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "util/ByteOrder.hxx"
#include "util/Cancellable.hxx"
#include "util/Compiler.h"
#include "util/MimeType.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "memory/istream_gb.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http_request.hxx"
#include "stopwatch.hxx"

#include <inttypes.h>
#include <stdarg.h>

class LbPrometheusExporter::AppendRequest final
	: public HttpResponseHandler, Cancellable
{
	DelayedIstreamControl &control;

	HttpAddress address{false, "dummy:80", "/"};

	CancellablePointer cancel_ptr;

public:
	AppendRequest(SocketAddress _address,
		      DelayedIstreamControl &_control) noexcept
		:control(_control)
	{
		control.cancel_ptr = *this;

		address.addresses.AddPointer(_address);
	}

	void Start(struct pool &pool, LbInstance &instance) noexcept;

	void Destroy() noexcept {
		this->~AppendRequest();
	}

	void DestroyError(std::exception_ptr error) noexcept {
		auto &_control = control;
		Destroy();
		_control.SetError(std::move(error));
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;

	void OnHttpError(std::exception_ptr error) noexcept override {
		DestroyError(std::move(error));
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}
};

inline void
LbPrometheusExporter::AppendRequest::Start(struct pool &pool,
					   LbInstance &instance) noexcept
{
	http_request(pool, instance.event_loop,
		     *instance.fs_balancer, {}, {},
		     nullptr,
		     HTTP_METHOD_GET, address, {}, nullptr,
		     *this, cancel_ptr);
}

void
LbPrometheusExporter::AppendRequest::OnHttpResponse(http_status_t status,
						    StringMap &&headers,
						    UnusedIstreamPtr body) noexcept
try {
	if (!http_status_is_success(status))
		throw std::runtime_error("HTTP request not sucessful");

	const char *content_type = headers.Get("content-type");
	if (content_type == nullptr ||
	    GetMimeTypeBase(content_type) != "text/plain")
		throw std::runtime_error("Not text/plain");

	control.Set(std::move(body));
} catch (...) {
	DestroyError(std::current_exception());
}

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
	       R"(
# HELP beng_proxy_connections Number of connections
# TYPE beng_proxy_connections gauge

# HELP beng_proxy_children Number of child processes
# TYPE beng_proxy_children gauge

# HELP beng_proxy_sessions Number of sessions
# TYPE beng_proxy_sessions gauge

# HELP beng_proxy_http_requests Number of HTTP requests
# TYPE beng_proxy_http_requests counter

# HELP beng_proxy_http_traffic Number of bytes transferred
# TYPE beng_proxy_http_traffic counter

# HELP beng_proxy_cache_size Size of the cache in bytes
# TYPE beng_proxy_cache_size gauge

# HELP beng_proxy_buffer_size Size of buffers in bytes
# TYPE beng_proxy_buffer_size gauge

)"
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
	auto &pool = request.pool;

	GrowingBuffer buffer;

	const char *process = "lb";
	if (instance != nullptr)
		Write(buffer, process, instance->GetStats());

	HttpHeaders headers;
	headers.Write("content-type", "text/plain;version=0.0.4");

	auto body = NewConcatIstream(pool,
				     istream_gb_new(pool, std::move(buffer)));

	for (const auto &i : config.load_from_local) {
		// TODO check instance!=nullptr
		auto delayed = istream_delayed_new(pool, instance->event_loop);
		AppendConcatIstream(body, std::move(delayed.first));

		auto *ar = NewFromPool<AppendRequest>(pool, i, delayed.second);
		ar->Start(pool, *instance);
	}

	request.SendResponse(HTTP_STATUS_OK, std::move(headers),
			     std::move(body));
}
