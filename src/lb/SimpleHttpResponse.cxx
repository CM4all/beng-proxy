/*
 * Copyright 2007-2019 CM4all GmbH
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

#include "SimpleHttpResponse.hxx"
#include "http/IncomingRequest.hxx"
#include "RedirectHttps.hxx"

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
		const char *host = request.headers.Get("host");
		if (host == nullptr) {
			request.SendSimpleResponse(HTTP_STATUS_BAD_REQUEST,
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
