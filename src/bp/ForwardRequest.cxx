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

#include "ForwardRequest.hxx"
#include "Request.hxx"
#include "Connection.hxx"
#include "http/IncomingRequest.hxx"

ForwardRequest
Request::ForwardRequest(const HeaderForwardSettings &header_forward,
			bool exclude_host) noexcept
{
	assert(!request.HasBody() || request_body);

	http_method_t method;
	UnusedIstreamPtr body;

	/* send a request body? */

	if (processor_focus) {
		/* reserve method+body for the processor, and
		   convert this request to a GET */

		method = HTTP_METHOD_GET;
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
				      !IsTransformationEnabled(),
				      !IsTransformationEnabled(),
				      header_forward,
				      GetCookieHost(), GetCookieURI()),
		std::move(body),
	};
}
