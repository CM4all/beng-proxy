// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ForwardRequest.hxx"
#include "Request.hxx"
#include "Connection.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"

ForwardRequest
Request::ForwardRequest(const HeaderForwardSettings &header_forward,
			bool exclude_host) noexcept
{
	assert(!request.HasBody() || request_body);

	HttpMethod method;
	UnusedIstreamPtr body;

	/* send a request body? */

	if (processor_focus) {
		/* reserve method+body for the processor, and
		   convert this request to a GET */

		method = HttpMethod::GET;
	} else {
		/* forward body (if any) to the real server */

		method = request.method;

		/* in TRANSPARENT_CHAIN mode, don't send the request
		   body to the handler; instead, send it to the
		   chained (following) request handler */
		if (!translate.response->transparent_chain)
			body = std::move(request_body);
	}

	/* generate request headers */

	const bool has_body = body;

	return ::ForwardRequest{
		method,
		ForwardRequestHeaders(request.headers,
				      exclude_host,
				      has_body,
				      !IsProcessorEnabled(),
				      !IsTransformationEnabled() && !translate.response->HasAutoCompress(),
				      !IsTransformationEnabled(),
				      header_forward,
				      GetCookieHost(), GetCookieURI()),
		std::move(body),
	};
}
