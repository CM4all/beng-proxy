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

#include "Internal.hxx"
#include "Request.hxx"
#include "Handler.hxx"
#include "http/Upgrade.hxx"
#include "pool/pool.hxx"
#include "strmap.hxx"
#include "http/HeaderParser.hxx"
#include "istream/istream_null.hxx"
#include "http/List.hxx"
#include "util/StringCompare.hxx"
#include "util/StringStrip.hxx"
#include "util/StringView.hxx"
#include "util/StringFormat.hxx"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

inline bool
HttpServerConnection::ParseRequestLine(const char *line, size_t length)
{
	assert(request.read_state == Request::START);
	assert(request.request == nullptr);
	assert(!response.pending_drained);

	if (gcc_unlikely(length < 5)) {
		ProtocolError("malformed request line");
		return false;
	}

	const char *eol = line + length;

	http_method_t method = HTTP_METHOD_NULL;
	switch (line[0]) {
	case 'D':
		if (gcc_likely(line[1] == 'E' && line[2] == 'L' && line[3] == 'E' &&
			       line[4] == 'T' && line[5] == 'E' && line[6] == ' ')) {
			method = HTTP_METHOD_DELETE;
			line += 7;
		}
		break;

	case 'G':
		if (gcc_likely(line[1] == 'E' && line[2] == 'T' && line[3] == ' ')) {
			method = HTTP_METHOD_GET;
			line += 4;
		}
		break;

	case 'P':
		if (gcc_likely(line[1] == 'O' && line[2] == 'S' && line[3] == 'T' &&
			       line[4] == ' ')) {
			method = HTTP_METHOD_POST;
			line += 5;
		} else if (line[1] == 'U' && line[2] == 'T' && line[3] == ' ') {
			method = HTTP_METHOD_PUT;
			line += 4;
		} else if (auto patch = StringAfterPrefix(line + 1, "ATCH ")) {
			method = HTTP_METHOD_PATCH;
			line = patch;
		} else if (auto propfind = StringAfterPrefix(line + 1, "ROPFIND ")) {
			method = HTTP_METHOD_PROPFIND;
			line = propfind;
		} else if (auto proppatch = StringAfterPrefix(line + 1, "ROPPATCH ")) {
			method = HTTP_METHOD_PROPPATCH;
			line = proppatch;
		}

		break;

	case 'R':
		if (auto report = StringAfterPrefix(line + 1, "EPORT ")) {
			method = HTTP_METHOD_REPORT;
			line = report;
		}

		break;

	case 'H':
		if (gcc_likely(line[1] == 'E' && line[2] == 'A' && line[3] == 'D' &&
			       line[4] == ' ')) {
			method = HTTP_METHOD_HEAD;
			line += 5;
		}
		break;

	case 'O':
		if (auto options = StringAfterPrefix(line + 1, "PTIONS ")) {
			method = HTTP_METHOD_OPTIONS;
			line = options;
		}
		break;

	case 'T':
		if (auto trace = StringAfterPrefix(line + 1, "RACE ")) {
			method = HTTP_METHOD_TRACE;
			line = trace;
		}
		break;

	case 'M':
		if (auto mkcol = StringAfterPrefix(line + 1, "KCOL ")) {
			method = HTTP_METHOD_MKCOL;
			line = mkcol;
		} else if (auto move = StringAfterPrefix(line + 1, "OVE ")) {
			method = HTTP_METHOD_MOVE;
			line = move;
		}
		break;

	case 'C':
		if (auto copy = StringAfterPrefix(line + 1, "OPY ")) {
			method = HTTP_METHOD_COPY;
			line = copy;
		}
		break;

	case 'L':
		if (auto lock = StringAfterPrefix(line + 1, "OCK ")) {
			method = HTTP_METHOD_LOCK;
			line = lock;
		}
		break;

	case 'U':
		if (auto unlock = StringAfterPrefix(line + 1, "NLOCK ")) {
			method = HTTP_METHOD_UNLOCK;
			line = unlock;
		}
		break;
	}

	if (method == HTTP_METHOD_NULL) {
		/* invalid request method */

		ProtocolError("unrecognized request method");
		return false;
	}

	const char *space = (const char *)memchr(line, ' ', eol - line);
	if (gcc_unlikely(space == nullptr || space + 6 > line + length ||
			 memcmp(space + 1, "HTTP/", 5) != 0)) {
		/* refuse HTTP 0.9 requests */
		static const char msg[] =
			"This server requires HTTP 1.1.";

		ssize_t nbytes = socket->Write(msg, sizeof(msg) - 1);
		if (nbytes != WRITE_DESTROYED)
			Done();
		return false;
	}

	request.request = http_server_request_new(this, method, {line, space});
	request.read_state = Request::HEADERS;

	return true;
}

/**
 * @return false if the connection has been closed
 */
inline bool
HttpServerConnection::HeadersFinished()
{
	assert(request.body_state == Request::BodyState::START);

	auto &r = *request.request;
	r.stopwatch.RecordEvent("request_headers");

	handler->RequestHeadersFinished(r);

	/* disable the idle+headers timeout; the request body timeout will
	   be tracked by filtered_socket (auto-refreshing) */
	idle_timeout.Cancel();

	const char *value = r.headers.Get("expect");
	request.expect_100_continue = value != nullptr &&
		strcmp(value, "100-continue") == 0;
	request.expect_failed = value != nullptr &&
		strcmp(value, "100-continue") != 0;

	value = r.headers.Get("connection");

	/* we disable keep-alive support on ancient HTTP 1.0, because that
	   feature was not well-defined and led to problems with some
	   clients */
	keep_alive = value == nullptr || !http_list_contains_i(value, "close");

	const bool upgrade = http_is_upgrade(r.headers);

	value = r.headers.Get("transfer-encoding");

	Event::Duration read_timeout = http_server_read_timeout;

	off_t content_length = -1;
	const bool chunked = value != nullptr && strcasecmp(value, "chunked") == 0;
	if (!chunked) {
		value = r.headers.Get("content-length");

		if (upgrade) {
			if (value != nullptr) {
				ProtocolError("cannot upgrade with Content-Length request header");
				return false;
			}

			/* disable timeout */
			read_timeout = Event::Duration(-1);

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
			if (gcc_unlikely(*endptr != 0 || content_length < 0)) {
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
	} else if (upgrade) {
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

	/* for the request body, the FilteredSocket class tracks
	   inactivity timeout */
	socket->ScheduleReadTimeout(false, read_timeout);

	return true;
}

/**
 * @return false if the connection has been closed
 */
inline bool
HttpServerConnection::HandleLine(StringView line) noexcept
{
	assert(request.read_state == Request::START ||
	       request.read_state == Request::HEADERS);

	if (line.size >= 8192) {
		ProtocolError(StringFormat<64>("Request header is too large (%zu)",
					       line.size));
		return false;
	}

	if (gcc_unlikely(request.read_state == Request::START)) {
		assert(request.request == nullptr);

		return ParseRequestLine(line.data, line.size);
	} else if (gcc_likely(!line.empty())) {
		assert(request.read_state == Request::HEADERS);
		assert(request.request != nullptr);

		header_parse_line(request.request->pool,
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
HttpServerConnection::FeedHeaders(const StringView b) noexcept
{
	assert(request.read_state == Request::START ||
	       request.read_state == Request::HEADERS);

	if (request.bytes_received >= 64 * 1024) {
		ProtocolError("too many request headers");
		return BufferedResult::CLOSED;
	}

	StringView remaining = b;
	while (true) {
		auto s = remaining.Split('\n');
		if (s.second == nullptr)
			break;

		StringView line = s.first;
		remaining = s.second;

		line.StripRight();

		if (!HandleLine(line))
			return BufferedResult::CLOSED;

		if (request.read_state != Request::HEADERS)
			break;
	}

	const size_t consumed = remaining.data - b.data;
	request.bytes_received += consumed;
	socket->DisposeConsumed(consumed);

	return request.read_state == Request::HEADERS
		? BufferedResult::MORE
		: BufferedResult::OK;
}

inline bool
HttpServerConnection::SubmitRequest()
{
	if (request.read_state == Request::END)
		/* re-enable the event, to detect client disconnect while
		   we're processing the request */
		socket->ScheduleReadNoTimeout(false);

	const DestructObserver destructed(*this);

	if (request.expect_failed) {
		request.request->body.Clear();
		request.request->SendMessage(HTTP_STATUS_EXPECTATION_FAILED,
					     "Unrecognized expectation");
		if (destructed)
			return false;
	} else {
		request.in_handler = true;
		handler->HandleHttpRequest(*request.request,
					   request.request->stopwatch,
					   request.cancel_ptr);
		if (destructed)
			return false;

		request.in_handler = false;
	}

	return true;
}

BufferedResult
HttpServerConnection::Feed(ConstBuffer<void> b) noexcept
{
	assert(!response.pending_drained);

	switch (request.read_state) {
		BufferedResult result;

	case Request::START:
		if (score == HTTP_SERVER_NEW)
			score = HTTP_SERVER_FIRST;

		[[fallthrough]];

	case Request::HEADERS:
		result = FeedHeaders(StringView(b));
		if (result == BufferedResult::OK &&
		    (request.read_state == Request::BODY ||
		     request.read_state == Request::END)) {
			if (request.read_state == Request::BODY)
				result = request_body_reader->RequireMore()
					? BufferedResult::AGAIN_EXPECT
					: BufferedResult::AGAIN_OPTIONAL;

			if (!SubmitRequest())
				result = BufferedResult::CLOSED;
		}

		return result;

	case Request::BODY:
		return FeedRequestBody(b.data, b.size);

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
			socket->DisposeConsumed(b.size);
			return BufferedResult::OK;
		}

		return BufferedResult::MORE;
	}

	assert(false);
	gcc_unreachable();
}

DirectResult
HttpServerConnection::TryRequestBodyDirect(SocketDescriptor fd, FdType fd_type)
{
	assert(IsValid());
	assert(request.read_state == Request::BODY);
	assert(!response.pending_drained);

	if (!MaybeSend100Continue())
		return DirectResult::CLOSED;

	ssize_t nbytes = request_body_reader->TryDirect(fd, fd_type);
	if (nbytes == ISTREAM_RESULT_BLOCKING)
		/* the destination fd blocks */
		return DirectResult::BLOCKING;

	if (nbytes == ISTREAM_RESULT_CLOSED)
		/* the stream (and the whole connection) has been closed
		   during the direct() callback (-3); no further checks */
		return DirectResult::CLOSED;

	if (nbytes < 0) {
		if (errno == EAGAIN)
			return DirectResult::EMPTY;

		return DirectResult::ERRNO;
	}

	if (nbytes == ISTREAM_RESULT_EOF)
		return DirectResult::END;

	request.bytes_received += nbytes;

	if (request_body_reader->IsEOF()) {
		request.read_state = Request::END;
#ifndef NDEBUG
		request.body_state = Request::BodyState::CLOSED;
#endif

		const DestructObserver destructed(*this);
		request_body_reader->DestroyEof();
		return destructed
			? DirectResult::CLOSED
			: DirectResult::OK;
	} else {
		return DirectResult::OK;
	}
}

void
HttpServerConnection::OnDeferredRead() noexcept
{
	socket->Read(false);
}
