// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SimpleHttpResponse.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "uri/RedirectHttps.hxx"
#include "http/Status.hxx"
#include "AllocatorPtr.hxx"

void
SendResponse(IncomingHttpRequest &request,
	     const LbSimpleHttpResponse &response) noexcept
{
	assert(response.IsDefined());

	const char *location = nullptr;
	const char *message = response.message.empty()
		? nullptr
		: response.message.c_str();

	if (response.redirect_https) {
		const char *host = request.headers.Get(host_header);
		if (host == nullptr) {
			request.SendSimpleResponse(HttpStatus::BAD_REQUEST,
						   nullptr,
						   "No Host header");
			return;
		}

		location = MakeHttpsRedirect(AllocatorPtr{request.pool},
					     host, 443, request.uri);
		if (message == nullptr)
			message = "This page requires \"https\"";
	} else if (!response.location.empty())
		location = response.location.c_str();

	request.SendSimpleResponse(response.status, location, message);
}
