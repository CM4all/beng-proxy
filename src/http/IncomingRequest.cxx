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

#include "IncomingRequest.hxx"
#include "Logger.hxx"
#include "pool/pool.hxx"
#include "istream/istream_string.hxx"
#include "http/Headers.hxx"
#include "util/StringView.hxx"

IncomingHttpRequest::IncomingHttpRequest(PoolPtr &&_pool,
					 SocketAddress _local_address,
					 SocketAddress _remote_address,
					 const char *_local_host_and_port,
					 const char *_remote_host) noexcept
	:pool(std::move(_pool)),
	 local_address(_local_address),
	 remote_address(_remote_address),
	 local_host_and_port(_local_host_and_port),
	 remote_host(_remote_host),
	 method(HTTP_METHOD_NULL),
	 uri(nullptr)
{
}

IncomingHttpRequest::IncomingHttpRequest(PoolPtr &&_pool,
					 SocketAddress _local_address,
					 SocketAddress _remote_address,
					 const char *_local_host_and_port,
					 const char *_remote_host,
					 http_method_t _method,
					 StringView _uri) noexcept
	:pool(std::move(_pool)),
	 local_address(_local_address),
	 remote_address(_remote_address),
	 local_host_and_port(_local_host_and_port),
	 remote_host(_remote_host),
	 method(_method),
	 uri(p_strdup(pool, _uri))
{
}

IncomingHttpRequest::~IncomingHttpRequest() noexcept
{
	if (logger != nullptr)
		logger->~IncomingHttpRequestLogger();
}

void
IncomingHttpRequest::SendSimpleResponse(http_status_t status,
					const char *location,
					const char *msg) noexcept
{
	assert(unsigned(status) >= 200 && unsigned(status) < 600);

	if (http_status_is_empty(status))
		msg = nullptr;
	else if (msg == nullptr)
		msg = http_status_to_string(status);

	HttpHeaders response_headers;
	response_headers.generate_date_header = true;

	if (location != nullptr)
		response_headers.Write("location", location);

	UnusedIstreamPtr response_body;
	if (msg != nullptr) {
		response_headers.Write("content-type", "text/plain");
		response_body = istream_string_new(pool, msg);
	}

	SendResponse(status, std::move(response_headers),
		     std::move(response_body));
}

void
IncomingHttpRequest::SendMessage(http_status_t status, const char *msg) noexcept
{
	HttpHeaders response_headers;
	response_headers.generate_date_header = true;

	response_headers.Write("content-type", "text/plain");

	SendResponse(status, std::move(response_headers),
		     istream_string_new(pool, msg));
}

void
IncomingHttpRequest::SendRedirect(http_status_t status, const char *location,
				  const char *msg) noexcept
{
	assert(status >= 300 && status < 400);
	assert(location != nullptr);

	if (http_status_is_empty(status))
		msg = nullptr;
	else if (msg == nullptr)
		msg = http_status_to_string(status);

	HttpHeaders response_headers;
	response_headers.generate_date_header = true;

	response_headers.Write("content-type", "text/plain");
	response_headers.Write("location", location);

	SendResponse(status, std::move(response_headers),
		     istream_string_new(pool, msg));
}
