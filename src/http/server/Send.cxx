// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Internal.hxx"
#include "Request.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/Upgrade.hxx"
#include "http/Logger.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/istream_gb.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/ChunkedIstream.hxx"
#include "istream/DechunkIstream.hxx"
#include "istream/istream_memory.hxx"
#include "http/Date.hxx"
#include "event/Loop.hxx"
#include "net/log/ContentType.hxx"
#include "util/SpanCast.hxx"
#include "product.h"

#include <fmt/format.h> // for fmt::format_int

#include <string.h>

using std::string_view_literals::operator""sv;

bool
HttpServerConnection::Send100Continue() noexcept
{
	assert(IsValid());
	assert(request.read_state == Request::BODY);
	assert(!HasInput());
	assert(!request.expect_100_continue);

	/* this string is simple enough to expect that we don't need to
	   check for partial writes, not before we have sent a single byte
	   of response to the peer */
	static constexpr auto response_string = "HTTP/1.1 100 Continue\r\n\r\n"sv;
	ssize_t nbytes = socket->Write(std::as_bytes(std::span{response_string}));
	if (nbytes == (ssize_t)response_string.size()) [[likely]] {
		/* re-enable the request body read timeout that was
		   disabled by HeadersFinished() in the presence of an
		   "expect:100-continue" request header */
		ScheduleReadTimeoutTimer();
		return true;
	}

	if (nbytes == WRITE_ERRNO)
		SocketErrorErrno("write error");
	else if (nbytes != WRITE_DESTROYED)
		SocketError("write error");
	return false;
}

bool
HttpServerConnection::MaybeSend100Continue() noexcept
{
	assert(IsValid());
	assert(request.read_state == Request::BODY);

	if (!request.expect_100_continue)
		return true;

	assert(!HasInput());

	request.expect_100_continue = false;
	return Send100Continue();
}

static void
PrependStatusLine(GrowingBuffer &buffer, HttpStatus status) noexcept
{
	assert(http_status_is_valid(status));

	static constexpr std::string_view protocol = "HTTP/1.1 "sv;
	const std::string_view status_string = http_status_to_string(status);
	const std::size_t status_line_length = protocol.size() + status_string.size() + 2;

	char *p = (char *)buffer.Prepend(status_line_length);
	p = std::copy(protocol.begin(), protocol.end(), p);
	p = std::copy(status_string.begin(), status_string.end(), p);
	*p++ = '\r';
	*p++ = '\n';
}

inline void
HttpServerConnection::SubmitResponse(HttpStatus status,
				     HttpHeaders &&headers,
				     UnusedIstreamPtr body)
{
	assert(http_status_is_valid(status));
	assert(score != HTTP_SERVER_NEW);
	assert(socket->IsConnected());
	assert(request.read_state == Request::END ||
	       request.body_state == Request::BodyState::READING);

	request.cancel_ptr = nullptr;

	request.request->stopwatch.RecordEvent("response_headers");

	if (http_status_is_success(status)) {
		if (score == HTTP_SERVER_FIRST)
			score = HTTP_SERVER_SUCCESS;
	} else {
		score = HTTP_SERVER_ERROR;
	}

	if (request.read_state == HttpServerConnection::Request::BODY &&
	    /* if we didn't send "100 Continue" yet, we should do it now;
	       we don't know if the request body will be used, but at
	       least it hasn't been closed yet */
	    !MaybeSend100Continue())
		return;

	auto &request_pool = request.request->pool;

	response.status = status;

	if (request.request->logger != nullptr && request.request->logger->WantsContentType()) {
		if (const auto content_type = headers.GetSloppy(content_type_header);
		    !content_type.empty())
			response.content_type = Net::Log::ParseContentType(content_type);
	}

	PrependStatusLine(headers.GetBuffer(), status);

	/* how will we transfer the body?  determine length and
	   transfer-encoding */

	const bool got_body = body;

	const auto body_length = got_body
		? body.GetLength()
		: IstreamLength{.length = 0, .exhaustive = true};

	if (http_method_is_empty(request.request->method))
		body.Clear();

	if (!body_length.exhaustive) {
		/* the response length is unknown yet */
		assert(!http_status_is_empty(status));

		if (body && keep_alive) {
			/* keep-alive is enabled, which means that we have to
			   enable chunking */
			headers.Write("transfer-encoding", "chunked");

			/* optimized code path: if an istream_dechunked shall get
			   chunked via istream_chunk, let's just skip both to
			   reduce the amount of work and I/O we have to do */
			body = istream_chunked_new(request_pool, std::move(body));
		}
	} else if (http_status_is_empty(status)) {
		assert(body_length.length == 0);
	} else if (got_body || !http_method_is_empty(request.request->method)) {
		/* fixed body size */
		headers.Write("content-length", fmt::format_int{body_length.length}.c_str());
	}

	const bool upgrade = body && http_is_upgrade(status, headers);
	if (upgrade) {
		headers.Write("connection", "upgrade");
		headers.MoveToBuffer(upgrade_header);
	} else if (!keep_alive)
		headers.Write("connection", "close");

	if (headers.generate_date_header)
		/* RFC 2616 14.18: Date */
		headers.Write("date"sv, GetEventLoop().SystemNow());

	if (headers.generate_server_header)
		/* RFC 2616 3.8: Product Tokens */
		headers.Write("server", BRIEF_PRODUCT_TOKEN);

	if (request.request->generate_hsts_header)
		/* TODO: hard-coded to 90 days (7776000 seconds), but
		   this should probably be configurable */
		headers.Write("strict-transport-security", "max-age=7776000");

	GrowingBuffer headers3 = headers.ToBuffer();
	headers3.Write("\r\n"sv);

	/* make sure the access logger gets a negative value if there
	   is no response body */
	response.length -= !body;

#ifdef HAVE_URING
	if (auto *uring_queue = socket->GetUringQueue()) {
		assert(uring_send == nullptr);

		if (body)
			SetResponseIstream(std::move(body));

		StartUringSend(*uring_queue, std::move(headers3));
		return;
	}
#endif
	auto header_stream = istream_gb_new(request_pool, std::move(headers3));

	const auto header_length = header_stream.GetLength();
	assert(header_length.exhaustive);
	response.length = -header_length.length;

	SetResponseIstream(NewConcatIstream(request_pool, std::move(header_stream),
					    std::move(body)));
	DeferWrite();
}

void
HttpServerRequest::SendResponse(HttpStatus status,
				HttpHeaders &&response_headers,
				UnusedIstreamPtr response_body) noexcept
{
	assert(connection.request.request == this);

	connection.SubmitResponse(status, std::move(response_headers),
				  std::move(response_body));
}
