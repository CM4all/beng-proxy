// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "Precompressed.hxx"
#include "Connection.hxx"
#include "Config.hxx"
#include "Listener.hxx"
#include "Instance.hxx"
#include "PendingResponse.hxx"
#include "session/Lease.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "widget/Context.hxx"
#include "translation/Vary.hxx"
#include "uri/Args.hxx"
#include "util/StringAPI.hxx"
#include "strmap.hxx"

Request::Request(BpConnection &_connection,
		 IncomingHttpRequest &_request,
		 const StopwatchPtr &parent_stopwatch) noexcept
	:PoolLeakDetector(_request.pool), pool(_request.pool),
	 instance(_connection.instance),
	 connection(_connection),
	 logger(connection.logger),
	 stopwatch(parent_stopwatch, "handler"),
	 request(_request),
	 request_body(pool, std::move(request.body)),
	 session_cookie_same_site(connection.config.session_cookie_same_site)
{
	session_id.Clear();
}

Request::~Request() noexcept = default;

TranslationService &
Request::GetTranslationService() const noexcept
{
	return connection.listener.GetTranslationService();
}

bool
Request::IsProcessorEnabled() const noexcept
{
	return translate.response->views.front().HasProcessor();
}

void
Request::ParseArgs() noexcept
{
	assert(args.IsEmpty());

	if (dissected_uri.args.empty()) {
		translate.request.param = nullptr;
		translate.request.session = {};
		return;
	}

	args = args_parse(pool, dissected_uri.args);

	/* obsolete as of version 15.29 */
	args.Remove("session");

	translate.request.param = args.Remove("translate");
	translate.request.session = {};
}

bool
Request::IsHttps() const noexcept
{
	if (connection.ssl)
		/* the connection to beng-proxy is already
		   SSL/TLS-encrypted */
		return true;

	const char *https = request.headers.Get(x_cm4all_https_header);
	return https != nullptr && StringIsEqual(https, "on");
}

const char *
Request::GetExternalUriScheme(const TranslateResponse &tr) const noexcept
{
	if (tr.scheme != nullptr)
		return tr.scheme;

	if (IsHttps())
		return "https";

	return "http";
}

const char *
Request::GetExternalUriHost(const TranslateResponse &tr) const noexcept
{
	if (tr.host != nullptr)
		return tr.host;

	const char *host = request.headers.Get(host_header);
	if (host == nullptr)
		/* lousy fallback for an RFC-ignorant browser */
		host = "localhost";

	return host;
}

StringMap
Request::ForwardRequestHeaders(const StringMap &src,
			       bool exclude_host,
			       bool with_body,
			       bool forward_charset,
			       bool forward_encoding,
			       bool forward_range,
			       const HeaderForwardSettings &settings,
			       const char *host_and_port,
			       const char *uri) noexcept
{
	return forward_request_headers(pool, src,
				       request.local_host_and_port,
				       request.remote_host,
				       connection.peer_subject,
				       connection.peer_issuer_subject,
				       exclude_host,
				       with_body,
				       forward_charset,
				       forward_encoding,
				       forward_range,
				       settings,
				       session_cookie,
				       GetRealmSession().get(),
				       user,
				       session_id.IsDefined() ? "1" : "0",
				       host_and_port, uri);
}

StringMap
Request::ForwardResponseHeaders(HttpStatus status,
				const StringMap &src,
				const char *(*relocate)(const char *uri,
							void *ctx) noexcept,
				void *relocate_ctx,
				const HeaderForwardSettings &settings) noexcept
{
	auto headers = forward_response_headers(pool, status, src,
						request.local_host_and_port,
						session_cookie,
						relocate, relocate_ctx,
						settings);

	add_translation_vary_header(pool, headers, *translate.response);

	product_token = headers.Remove(server_header);

	date = headers.Remove(date_header);

	return headers;
}
