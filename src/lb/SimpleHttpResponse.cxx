// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SimpleHttpResponse.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "uri/RedirectHttps.hxx"
#include "http/Status.hxx"
#include "AllocatorPtr.hxx"

using std::string_view_literals::operator""sv;

void
SendResponse(IncomingHttpRequest &request,
	     const LbSimpleHttpResponse &response) noexcept
{
	assert(response.IsDefined());

	std::string_view location{};
	std::string_view message = response.message;
	if (message.empty())
		message = {};

	if (response.redirect_https) {
		const char *host = request.headers.Get(host_header);
		if (host == nullptr) {
			request.SendSimpleResponse(HttpStatus::BAD_REQUEST,
						   {},
						   "No Host header"sv);
			return;
		}

		location = MakeHttpsRedirect(AllocatorPtr{request.pool},
					     host, 443, request.uri);
		if (message.data() == nullptr)
			message = "This page requires \"https\""sv;
	} else if (!response.location.empty())
		location = response.location.c_str();

	request.SendSimpleResponse(response.status, location, message);
}
