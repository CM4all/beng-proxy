// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IncomingRequest.hxx"
#include "Logger.hxx"
#include "pool/pool.hxx"
#include "istream/istream_string.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"

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
	 method(HttpMethod::UNDEFINED),
	 uri(nullptr)
{
}

IncomingHttpRequest::IncomingHttpRequest(PoolPtr &&_pool,
					 SocketAddress _local_address,
					 SocketAddress _remote_address,
					 const char *_local_host_and_port,
					 const char *_remote_host,
					 HttpMethod _method,
					 std::string_view _uri) noexcept
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
IncomingHttpRequest::SendSimpleResponse(HttpStatus status,
					std::string_view location,
					std::string_view msg) noexcept
{
	assert(http_status_is_valid(status));

	if (http_status_is_empty(status))
		msg = {};
	else if (msg.data() == nullptr)
		msg = http_status_to_string(status);

	HttpHeaders response_headers;

	if (location.data() != nullptr)
		response_headers.Write("location", location);

	UnusedIstreamPtr response_body;
	if (msg.data() != nullptr) {
		response_headers.Write("content-type", "text/plain");
		response_body = istream_string_new(pool, msg);
	}

	SendResponse(status, std::move(response_headers),
		     std::move(response_body));
}

void
IncomingHttpRequest::SendMessage(HttpStatus status, std::string_view msg) noexcept
{
	HttpHeaders response_headers;

	response_headers.Write("content-type", "text/plain");

	SendResponse(status, std::move(response_headers),
		     istream_string_new(pool, msg));
}

void
IncomingHttpRequest::SendRedirect(HttpStatus status, std::string_view location,
				  std::string_view msg) noexcept
{
	assert(http_status_is_redirect(status));

	if (http_status_is_empty(status))
		msg = {};
	else if (msg.data() == nullptr)
		msg = http_status_to_string(status);

	HttpHeaders response_headers;

	response_headers.Write("content-type", "text/plain");
	response_headers.Write("location", location);

	SendResponse(status, std::move(response_headers),
		     istream_string_new(pool, msg));
}
