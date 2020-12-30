/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Request.hxx"
#include "Connection.hxx"
#include "Listener.hxx"
#include "PendingResponse.hxx"
#include "http/IncomingRequest.hxx"
#include "widget/Context.hxx"
#include "util/StringAPI.hxx"
#include "translation/Vary.hxx"
#include "args.hxx"
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
	 request_body(pool, std::move(request.body))
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
	return translate.response->views->HasProcessor();
}

void
Request::ParseArgs() noexcept
{
	assert(args.IsEmpty());

	if (dissected_uri.args.empty()) {
		translate.request.param = nullptr;
		translate.request.session = nullptr;
		return;
	}

	args = args_parse(pool, dissected_uri.args);

	/* obsolete as of version 15.29 */
	args.Remove("session");

	translate.request.param = args.Remove("translate");
	translate.request.session = nullptr;
}

bool
Request::IsHttps() const noexcept
{
	const char *https = request.headers.Get("x-cm4all-https");
	return https != nullptr && StringIsEqual(https, "on");
}

const char *
Request::GetExternalUriScheme(const TranslateResponse &tr) const noexcept
{
	if (tr.scheme != nullptr)
		return tr.scheme;

	return "http";
}

const char *
Request::GetExternalUriHost(const TranslateResponse &tr) const noexcept
{
	if (tr.host != nullptr)
		return tr.host;

	const char *host = request.headers.Get("host");
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
				       host_and_port, uri);
}

StringMap
Request::ForwardResponseHeaders(http_status_t status,
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

	product_token = headers.Remove("server");

#ifdef NO_DATE_HEADER
	date = headers.Remove("date");
#endif

	return headers;
}
