// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Internal.hxx"
#include "Request.hxx"
#include "Handler.hxx"
#include "http/CommonHeaders.hxx"
#include "http/HeaderLimits.hxx"
#include "http/Method.hxx"
#include "http/Upgrade.hxx"
#include "pool/pool.hxx"
#include "strmap.hxx"
#include "http/HeaderParser.hxx"
#include "istream/istream_null.hxx"
#include "http/List.hxx"
#include "util/SpanCast.hxx"
#include "util/StringCompare.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <utility> // for std::unreachable()

#include <string.h>
#include <stdlib.h>
#include <errno.h>

using std::string_view_literals::operator""sv;

/**
 * Parse the HTTP request method at the beginning of the given string
 * and return it, together with a pointer to the first character after
 * the space after the method.
 *
 * Returns HttpMethod{} if the method was not recognized.
 */
[[gnu::pure]]
static std::pair<HttpMethod, const char *>
ParseHttpMethod(const char *s) noexcept
{
	switch (s[0]) {
	case 'D':
		if (s[1] == 'E' && s[2] == 'L' && s[3] == 'E' &&
		    s[4] == 'T' && s[5] == 'E' && s[6] == ' ') [[likely]]
			return {HttpMethod::DELETE, s + 7};

		break;

	case 'G':
		if (s[1] == 'E' && s[2] == 'T' && s[3] == ' ') [[likely]]
			return {HttpMethod::GET, s + 4};

		break;

	case 'P':
		if (s[1] == 'O' && s[2] == 'S' && s[3] == 'T' &&
		    s[4] == ' ') [[likely]]
			return {HttpMethod::POST, s + 5};
		else if (s[1] == 'U' && s[2] == 'T' && s[3] == ' ')
			return {HttpMethod::PUT, s + 4};
		else if (auto patch = StringAfterPrefix(s + 1, "ATCH "))
			return {HttpMethod::PATCH, patch};
		else if (auto propfind = StringAfterPrefix(s + 1, "ROPFIND "))
			return {HttpMethod::PROPFIND, propfind};
		else if (auto proppatch = StringAfterPrefix(s + 1, "ROPPATCH "))
			return {HttpMethod::PROPPATCH, proppatch};

		break;

	case 'R':
		if (auto report = StringAfterPrefix(s + 1, "EPORT "))
			return {HttpMethod::REPORT, report};

		break;

	case 'H':
		if (s[1] == 'E' && s[2] == 'A' && s[3] == 'D' &&
		    s[4] == ' ') [[likely]]
			return {HttpMethod::HEAD, s + 5};

		break;

	case 'O':
		if (auto options = StringAfterPrefix(s + 1, "PTIONS "))
			return {HttpMethod::OPTIONS, options};

		break;

	case 'T':
		if (auto trace = StringAfterPrefix(s + 1, "RACE "))
			return {HttpMethod::TRACE, trace};

		break;

	case 'M':
		if (auto mkcol = StringAfterPrefix(s + 1, "KCOL "))
			return {HttpMethod::MKCOL, mkcol};
		else if (auto move = StringAfterPrefix(s + 1, "OVE "))
			return {HttpMethod::MOVE, move};

		break;

	case 'C':
		if (auto copy = StringAfterPrefix(s + 1, "OPY "))
			return {HttpMethod::COPY, copy};

		break;

	case 'L':
		if (auto lock = StringAfterPrefix(s + 1, "OCK "))
			return {HttpMethod::LOCK, lock};

		break;

	case 'U':
		if (auto unlock = StringAfterPrefix(s + 1, "NLOCK "))
			return {HttpMethod::UNLOCK, unlock};

		break;
	}

	return {};
}

inline bool
HttpServerConnection::ParseRequestLine(std::string_view line) noexcept
{
	assert(request.read_state == Request::START);
	assert(request.request == nullptr);
	assert(!response.pending_drained);

	if (line.size() < 5) [[unlikely]] {
		ProtocolError("malformed request line");
		return false;
	}

	const auto [method, rest] = ParseHttpMethod(line.data());
	if (method == HttpMethod{}) {
		/* invalid request method */

		ProtocolError("unrecognized request method");
		return false;
	}

	line.remove_prefix(rest - line.data());

	const auto space = line.find(' ');
	if (space == line.npos || space + 6 > line.size() ||
	    memcmp(line.data() + space + 1, "HTTP/", 5) != 0) [[unlikely]] {
		/* refuse HTTP 0.9 requests */
		static constexpr auto msg =
			"This server requires HTTP 1.1."sv;

		ssize_t nbytes = socket->Write(std::as_bytes(std::span{msg}));
		if (nbytes != WRITE_DESTROYED)
			Done();
		return false;
	}

	auto uri = line.substr(0, space);

	if (uri.size() >= 8192) {
		request.SetError(HttpStatus::REQUEST_URI_TOO_LONG,
				 "Request URI is too long\n");
		request.ignore_headers = true;

		/* truncate the URI so it doesn't hog the logs */
		uri = uri.substr(0, 1024);
	}

	request.request = NewRequest(method, uri);
	request.read_state = Request::HEADERS;

	return true;
}

/**
 * @return false if the connection has been closed
 */
inline bool
HttpServerConnection::HeadersFinished() noexcept
{
	assert(request.body_state == Request::BodyState::START);

	/* cancel the request_header_timeout */
	read_timer.Cancel();

	auto &r = *request.request;
	r.stopwatch.RecordEvent("request_headers");

	wait_tracker.Reset();

	handler->RequestHeadersFinished(r);

	/* disable the idle+headers timeout; the request body timeout will
	   be tracked by FilteredSocket (auto-refreshing) */
	idle_timer.Cancel();

	const char *value = r.headers.Get(expect_header);
	request.expect_100_continue = value != nullptr &&
		StringIsEqual(value, "100-continue");
	if (value != nullptr && !StringIsEqual(value, "100-continue"))
		request.SetError(HttpStatus::EXPECTATION_FAILED, "Unrecognized expectation\n");

	value = r.headers.Get(connection_header);
	keep_alive = value == nullptr || !http_list_contains_i(value, "close");

	request.upgrade = http_is_upgrade(r.headers);

	value = r.headers.Get(transfer_encoding_header);

	off_t content_length = -1;
	const bool chunked = value != nullptr && StringIsEqualIgnoreCase(value, "chunked");
	if (!chunked) {
		value = r.headers.Get(content_length_header);

		if (request.upgrade) {
			if (value != nullptr) {
				ProtocolError("cannot upgrade with Content-Length request header");
				return false;
			}

			/* forward incoming data as-is */

			keep_alive = false;
		} else if (value == nullptr) {
			/* no body at all */

			request.read_state = Request::END;
#ifndef NDEBUG
			request.body_state = Request::BodyState::NONE;
#endif

			return true;
		} else {
			char *endptr;

			content_length = strtoul(value, &endptr, 10);
			if (*endptr != 0 || content_length < 0) [[unlikely]] {
				ProtocolError("invalid Content-Length header in HTTP request");
				return false;
			}

			if (content_length == 0) {
				/* empty body */

				r.body = istream_null_new(r.pool);
				request.read_state = Request::END;
#ifndef NDEBUG
				request.body_state = Request::BodyState::EMPTY;
#endif

				return true;
			}
		}
	} else if (request.upgrade) {
		ProtocolError("cannot upgrade chunked request");
		return false;
	}

	request_body_reader = NewFromPool<RequestBodyReader>(r.pool, r.pool,
							     *this);
	r.body = request_body_reader->Init(GetEventLoop(), content_length,
					   chunked);

	request.read_state = Request::BODY;
#ifndef NDEBUG
	request.body_state = Request::BodyState::READING;
#endif

	return true;
}

/**
 * @return false if the connection has been closed
 */
inline bool
HttpServerConnection::HandleLine(std::string_view line) noexcept
{
	assert(request.read_state == Request::START ||
	       request.read_state == Request::HEADERS);

	if (request.read_state == Request::START) [[unlikely]] {
		assert(request.request == nullptr);

		return ParseRequestLine(line);
	} else if (!line.empty()) [[likely]] {
		assert(request.read_state == Request::HEADERS);
		assert(request.request != nullptr);

		if (request.ignore_headers)
			return true;

		if (line.size() >= MAX_HTTP_HEADER_SIZE) {
			request.SetError(HttpStatus::REQUEST_HEADER_FIELDS_TOO_LARGE,
					 "Request header is too long\n");
			request.ignore_headers = true;
			return true;
		}

		header_parse_line(*request.request->pool,
				  request.request->headers,
				  line);
		return true;
	} else {
		assert(request.read_state == Request::HEADERS);
		assert(request.request != nullptr);

		return HeadersFinished();
	}
}

inline BufferedResult
HttpServerConnection::FeedHeaders(const std::string_view b) noexcept
{
	assert(request.read_state == Request::START ||
	       request.read_state == Request::HEADERS);

	if (request.bytes_received >= MAX_TOTAL_HTTP_HEADER_SIZE) {
		assert(request.read_state == Request::HEADERS);

		socket->DisposeConsumed(b.size());

		request.SetError(HttpStatus::REQUEST_HEADER_FIELDS_TOO_LARGE,
				 "Too many request headers\n");
		HeadersFinished();

		/* reset the keep_alive flag after it was set by
		   HeadersFinished(); we need to disable keep-alive
		   because we're not parsing the rest of what we
		   received */
		keep_alive = false;

		/* pretend everything's ok; the actual error will be
		   generated by SubmitRequest() */
		return BufferedResult::OK;
	}

	std::string_view remaining = b;
	while (true) {
		auto [line, _remaining] = Split(remaining, '\n');
		if (_remaining.data() == nullptr)
			break;

		remaining = _remaining;

		line = StripRight(line);

		if (!HandleLine(line))
			return BufferedResult::DESTROYED;

		if (request.read_state != Request::HEADERS)
			break;
	}

	const std::size_t consumed = remaining.data() - b.data();
	request.bytes_received += consumed;
	socket->DisposeConsumed(consumed);

	return request.read_state == Request::HEADERS
		? BufferedResult::MORE
		: BufferedResult::OK;
}

inline bool
HttpServerConnection::SubmitRequest() noexcept
{
	const DestructObserver destructed(*this);

	if (request.error_status != HttpStatus{}) {
		request.request->body.Clear();
		request.request->SendMessage(request.error_status, request.error_message);
		if (destructed)
			return false;
	} else {
		request.in_handler = true;
		request_handler.HandleHttpRequest(*request.request,
						  request.request->stopwatch,
						  request.cancel_ptr);
		if (destructed)
			return false;

		request.in_handler = false;

		if (request.read_state == Request::BODY &&
		    socket->IsConnected()) {
			/* enable splice() if the handler supports
			   it */
			socket->SetDirect(request_body_reader->CheckDirect(socket->GetType()));

			ScheduleReadTimeoutTimer();
		}
	}

	return true;
}

BufferedResult
HttpServerConnection::Feed(std::span<const std::byte> b) noexcept
{
	assert(!response.pending_drained);

	switch (request.read_state) {
		BufferedResult result;

	case Request::START:
		if (score == HTTP_SERVER_NEW)
			score = HTTP_SERVER_FIRST;

		if (!read_timer.IsPending())
			read_timer.Schedule(request_header_timeout);

		[[fallthrough]];

	case Request::HEADERS:
		result = FeedHeaders(ToStringView(b));
		if (result == BufferedResult::OK &&
		    (request.read_state == Request::BODY ||
		     request.read_state == Request::END)) {
			if (request.read_state == Request::BODY)
				result = BufferedResult::AGAIN;

			if (!SubmitRequest())
				result = BufferedResult::DESTROYED;
		}

		return result;

	case Request::BODY:
		return FeedRequestBody(b);

	case Request::END:
		/* check if the connection was closed by the client while we
		   were processing the request */

		if (socket->IsFull())
			/* the buffer is full, the peer has been pipelining too
			   much - that would disallow us to detect a disconnect;
			   let's disable keep-alive now and discard all data */
			keep_alive = false;

		if (!keep_alive) {
			/* discard all pipelined input when keep-alive has been
			   disabled */
			socket->DisposeConsumed(b.size());
			return BufferedResult::OK;
		}

		return BufferedResult::MORE;
	}

	std::unreachable();
}

DirectResult
HttpServerConnection::TryRequestBodyDirect(SocketDescriptor fd, FdType fd_type) noexcept
{
	assert(IsValid());
	assert(request.read_state == Request::BODY);
	assert(!response.pending_drained);

	if (!MaybeSend100Continue())
		return DirectResult::CLOSED;

	const DestructObserver destructed{*this};

	switch (request_body_reader->TryDirect(fd, fd_type)) {
	case IstreamDirectResult::BLOCKING:
		/* the destination fd blocks */
		CancelReadTimeoutTimer();
		return DirectResult::BLOCKING;

	case IstreamDirectResult::CLOSED:
		/* the stream (and the whole connection) has been closed
		   during the direct() callback (-3); no further checks */
		return destructed
			? DirectResult::CLOSED
			: DirectResult::OK;

	case IstreamDirectResult::ERRNO:
		if (errno == EAGAIN)
			return DirectResult::EMPTY;

		return DirectResult::ERRNO;

	case IstreamDirectResult::END:
		return DirectResult::END;

	case IstreamDirectResult::OK:
		if (request_body_reader->IsEOF()) {
			request.read_state = Request::END;
#ifndef NDEBUG
			request.body_state = Request::BodyState::CLOSED;
#endif

			CancelReadTimeoutTimer();

			if (socket->IsConnected())
				socket->SetDirect(false);

			request_body_reader->DestroyEof();
			return destructed
				? DirectResult::CLOSED
				: DirectResult::OK;
		}

		/* refresh the request body timeout */
		ScheduleReadTimeoutTimer();

		return DirectResult::OK;

	case IstreamDirectResult::ASYNC:
		assert(!request_body_reader->IsEOF());
		return DirectResult::OK;
	}

	std::unreachable();
}
