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

#include "ForwardRequest.hxx"
#include "Request.hxx"
#include "Connection.hxx"
#include "http/IncomingRequest.hxx"

ForwardRequest
request_forward(Request &request2,
		const HeaderForwardSettings &header_forward,
		const char *host_and_port, const char *uri,
		bool exclude_host) noexcept
{
	const auto &request = request2.request;

	assert(!request.HasBody() || request2.request_body);

	http_method_t method;
	UnusedIstreamPtr body;

	/* send a request body? */

	if (request2.processor_focus) {
		/* reserve method+body for the processor, and
		   convert this request to a GET */

		method = HTTP_METHOD_GET;
	} else {
		/* forward body (if any) to the real server */

		method = request.method;
		body = std::move(request2.request_body);
	}

	/* generate request headers */

	const bool has_body = body;

	return ForwardRequest(method,
			      request2.ForwardRequestHeaders(request.headers,
							     exclude_host,
							     has_body,
							     !request2.IsProcessorEnabled(),
							     !request2.IsTransformationEnabled(),
							     !request2.IsTransformationEnabled(),
							     header_forward,
							     host_and_port, uri),
			      std::move(body));
}
