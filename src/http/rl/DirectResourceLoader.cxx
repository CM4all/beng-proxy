// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DirectResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "http/CommonHeaders.hxx"
#include "http/XForwardedFor.hxx"
#include "http/ResponseHandler.hxx"
#include "file/Address.hxx"
#include "file/Request.hxx"
#include "http/local/Glue.hxx"
#include "http/Address.hxx"
#include "cgi/Glue.hxx"
#include "cgi/Address.hxx"
#include "fcgi/Request.hxx"
#include "fcgi/Remote.hxx"
#include "was/Glue.hxx"
#include "was/MGlue.hxx"
#include "pipe_filter.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/StringStrip.hxx"

#include <string.h>

[[gnu::pure]]
static const char *
GetRemoteHost(const XForwardedForConfig &config, AllocatorPtr alloc,
	      const StringMap &headers) noexcept
{
	const char *xff = headers.Get(x_forwarded_for_header);
	if (xff == nullptr)
		return nullptr;

	const auto remote_host = config.GetRealRemoteHost(xff);
	if (remote_host.empty())
		return nullptr;

	return alloc.DupZ(remote_host);
}

void
DirectResourceLoader::SendRequest(struct pool &pool,
				  const StopwatchPtr &parent_stopwatch,
				  const ResourceRequestParams &params,
				  HttpMethod method,
				  const ResourceAddress &address,
				  StringMap &&headers,
				  UnusedIstreamPtr body,
				  HttpResponseHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
try {
	switch (address.type) {
		const FileAddress *file;
		const CgiAddress *cgi;

	case ResourceAddress::Type::NONE:
		break;

	case ResourceAddress::Type::LOCAL:
		/* static files cannot receive a request body, close it */
		body.Clear();

		file = &address.GetFile();

		static_file_get(event_loop,
#ifdef HAVE_URING
				uring,
#endif
				pool, file->base, file->path,
				file->content_type,
				false,
				handler, cancel_ptr);
		return;

	case ResourceAddress::Type::PIPE:
		cgi = &address.GetCgi();
		pipe_filter(spawn_service, event_loop, pool, parent_stopwatch,
			    cgi->path, cgi->args.ToArray(pool),
			    cgi->options,
			    params.status != HttpStatus{} ? params.status : HttpStatus::OK,
			    std::move(headers), std::move(body),
			    handler);
		return;

	case ResourceAddress::Type::CGI:
		cgi_new(spawn_service, event_loop, &pool, parent_stopwatch,
			method, &address.GetCgi(),
			GetRemoteHost(xff, pool, headers),
			headers, std::move(body),
			handler, cancel_ptr);
		return;

	case ResourceAddress::Type::FASTCGI: {
		cgi = &address.GetCgi();

		UniqueFileDescriptor stderr_fd;
		if (cgi->options.stderr_path != nullptr &&
		    !cgi->options.stderr_jailed) {
			stderr_fd = cgi->options.OpenStderrPath();
		}

		const char *remote_ip = GetRemoteHost(xff, pool, headers);

		if (cgi->address_list.empty())
			fcgi_request(&pool, fcgi_stock, parent_stopwatch,
				     params.site_name,
				     *cgi,
				     method,
				     remote_ip,
				     std::move(headers), std::move(body),
				     std::move(stderr_fd),
				     handler, cancel_ptr);
		else
			fcgi_remote_request(&pool, tcp_balancer,
					    parent_stopwatch,
					    *cgi,
					    method,
					    remote_ip,
					    std::move(headers), std::move(body),
					    std::move(stderr_fd),
					    handler, cancel_ptr);
		return;
	}

	case ResourceAddress::Type::WAS:
#ifdef HAVE_LIBWAS
		cgi = &address.GetCgi();

		if (cgi->concurrency == 0)
			was_request(pool, *was_stock, parent_stopwatch,
				    params.site_name,
				    *cgi,
				    GetRemoteHost(xff, pool, headers),
				    method,
				    std::move(headers), std::move(body),
				    params.want_metrics ? metrics_handler : nullptr,
				    handler, cancel_ptr);
		else if (!cgi->address_list.empty())
			SendRemoteWasRequest(pool, *remote_was_stock,
					     parent_stopwatch,
					     *cgi,
					     GetRemoteHost(xff, pool, headers),
					     method,
					     std::move(headers), std::move(body),
					     params.want_metrics ? metrics_handler : nullptr,
					     handler, cancel_ptr);
		else
			SendMultiWasRequest(pool, *multi_was_stock, parent_stopwatch,
					    params.site_name,
					    *cgi,
					    GetRemoteHost(xff, pool, headers),
					    method,
					    std::move(headers), std::move(body),
					    params.want_metrics ? metrics_handler : nullptr,
					    handler, cancel_ptr);
		return;
#else
		throw std::runtime_error("WAS support is disabled");
#endif

	case ResourceAddress::Type::HTTP:
		any_http_client.SendRequest(pool, parent_stopwatch,
					    params.sticky_hash,
					    method, address.GetHttp(),
					    std::move(headers),
					    std::move(body),
					    handler, cancel_ptr);
		return;

	case ResourceAddress::Type::LHTTP:
		lhttp_request(pool, event_loop, *lhttp_stock,
			      parent_stopwatch,
			      params.site_name,
			      address.GetLhttp(),
			      method, std::move(headers),
			      std::move(body),
			      handler, cancel_ptr);
		return;
	}

	/* the resource could not be located, abort the request */

	throw std::runtime_error("Could not locate resource");
} catch (...) {
	body.Clear();
	handler.InvokeError(std::current_exception());
}
