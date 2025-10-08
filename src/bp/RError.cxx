// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Convert C++ exception to a HTTP response.
 */

#include "Request.hxx"
#include "Instance.hxx"
#include "http/Client.hxx"
#include "cgi/Error.hxx"
#include "fcgi/Error.hxx"
#include "widget/Error.hxx"
#include "lib/openssl/Error.hxx"
#include "http/IncomingRequest.hxx"
#include "http/MessageHttpResponse.hxx"
#include "HttpMessageResponse.hxx"
#include "pool/pool.hxx"
#include "stock/Error.hxx"
#include "system/Error.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/TimeoutError.hxx"
#include "util/Exception.hxx"
#include "AllocatorPtr.hxx"

#include <string.h> // for strerror()

bool
Request::DispatchHttpMessageResponse(std::exception_ptr e) noexcept
{
	if (const auto *r = FindNested<HttpMessageResponse>(e)) {
		DispatchError(r->GetStatus(),
			      p_strdup(request.pool, r->what()));
		return true;
	}

	return false;
}

static MessageHttpResponse
Dup(struct pool &pool, HttpStatus status, const char *msg) noexcept
{
	return {status, p_strdup(&pool, msg)};
}

static constexpr MessageHttpResponse
ErrnoToResponse(int e) noexcept
{
	switch (e) {
	case ENOENT:
	case ENOTDIR:

	case ELOOP: /* RESOLVE_NO_SYMLINKS failed */
	case EXDEV: /* RESOLVE_BENEATH failed */
		return {HttpStatus::NOT_FOUND,
			"The requested file does not exist."};

	case EACCES:
	case EPERM:
		return {HttpStatus::FORBIDDEN,
			"Access to the requested file denied."};

	case ECONNREFUSED:
		return {HttpStatus::BAD_GATEWAY,
			"Connect to upstream server failed."};

	case ENETUNREACH:
	case EHOSTUNREACH:
		return {HttpStatus::BAD_GATEWAY,
			"Upstream server is unreachable."};

	case ETIMEDOUT:
		return {HttpStatus::BAD_GATEWAY,
			"Upstream server timed out"};

	case ENAMETOOLONG:
		return {HttpStatus::REQUEST_URI_TOO_LONG,
			"414 Request-URI Too Long"};

	case ESTALE:
		return {HttpStatus::SERVICE_UNAVAILABLE,
			"503 Service Unavailable"};

	default:
		return {};
	}
}

[[gnu::pure]]
static MessageHttpResponse
ToResponse(struct pool &pool, std::exception_ptr ep) noexcept
{
	if (const auto *r = FindNested<HttpMessageResponse>(ep))
		return Dup(pool, r->GetStatus(), r->what());

	if (const auto *e = FindNested<std::system_error>(ep)) {
		if (e->code().category() == ErrnoCategory()) {
			if (auto r = ErrnoToResponse(e->code().value());
			    r.status != HttpStatus{})
				return r;
		}
	}

	if (const auto *e = FindNested<WidgetError>(ep)) {
		switch (e->GetCode()) {
		case WidgetErrorCode::UNSPECIFIED:
			break;

		case WidgetErrorCode::WRONG_TYPE:
		case WidgetErrorCode::UNSUPPORTED_ENCODING:
			return {HttpStatus::BAD_GATEWAY, "Malformed widget response"};

		case WidgetErrorCode::NO_SUCH_VIEW:
			return {HttpStatus::NOT_FOUND, "No such view"};

		case WidgetErrorCode::NOT_A_CONTAINER:
			return Dup(pool, HttpStatus::NOT_FOUND, e->what());

		case WidgetErrorCode::FORBIDDEN:
			return {HttpStatus::FORBIDDEN, "Forbidden"};
		}
	}

	if (FindNested<HttpClientError>(ep) ||
	    /* a SslError is usually a failure to connect to the next
	       server */
	    FindNested<SslError>(ep))
		return {HttpStatus::BAD_GATEWAY, "Upstream server failed"};

	if (FindNested<FcgiClientError>(ep) ||
	    FindNested<CgiError>(ep))
		return {HttpStatus::BAD_GATEWAY, "Script failed"};

	if (FindNested<TimeoutError>(ep))
		return {HttpStatus::BAD_GATEWAY,
			"Upstream server timed out"};

	if (FindNested<SocketProtocolError>(ep))
		return {HttpStatus::BAD_GATEWAY,
			"Upstream server failed"};

	if (FindNested<StockOverloadedError>(ep))
		return {HttpStatus::TOO_MANY_REQUESTS,
			"Overloaded"};

	return {HttpStatus::INTERNAL_SERVER_ERROR, "Internal server error"};
}

void
Request::LogDispatchError(HttpStatus status,
			  std::string_view msg, std::string_view log_msg,
			  unsigned log_level) noexcept
{
	logger(log_level, "error on '", request.uri, "': ", log_msg);

	if (instance.config.verbose_response) {
		AllocatorPtr alloc{pool};
		msg = alloc.Dup(log_msg);
	}

	DispatchError(status, msg);
}

void
Request::LogDispatchError(HttpStatus status, std::string_view log_msg,
			  unsigned log_level) noexcept
{
	LogDispatchError(status, http_status_to_string(status),
			 log_msg, log_level);
}

void
Request::LogDispatchError(std::exception_ptr ep) noexcept
{
	if (DispatchHttpMessageResponse(ep))
		/* don't log this, just send the response directly and
		   return */
		return;

	auto response = ToResponse(pool, ep);
	if (instance.config.verbose_response)
		response.message = p_strdup(pool, GetFullMessage(ep));

	logger(response.status == HttpStatus::INTERNAL_SERVER_ERROR ? 1 : 2,
	       "error on '", request.uri, "': ", ep);

	DispatchError(response.status, response.message);
}

void
Request::LogDispatchError(HttpStatus status, std::string_view msg,
			  std::exception_ptr ep, unsigned log_level) noexcept
{
	if (DispatchHttpMessageResponse(ep))
		/* don't log this, just send the response directly and
		   return */
		return;

	logger(log_level, "error on '", request.uri, "': ", ep);

	if (instance.config.verbose_response)
		msg = p_strdup(pool, GetFullMessage(ep));

	DispatchError(status, msg);
}

void
Request::LogDispatchErrno(int error, std::string_view msg) noexcept
{
	auto response = ErrnoToResponse(error);
	if (response.status == HttpStatus{})
		response = {HttpStatus::INTERNAL_SERVER_ERROR, "Internal server error"};

	if (instance.config.verbose_response)
		response.message = strerror(error);

	logger(response.status == HttpStatus::INTERNAL_SERVER_ERROR ? 1 : 2,
	       "error on '", request.uri, "': ", msg, ": ", strerror(error));

	DispatchError(response.status, response.message);
}
