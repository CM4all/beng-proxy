// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Client.hxx"
#include "Error.hxx"
#include "Parser.hxx"
#include "Protocol.hxx"
#include "Serialize.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/istream_gb.hxx"
#include "http/ResponseHandler.hxx"
#include "istream_fcgi.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/Bucket.hxx"
#include "http/CommonHeaders.hxx"
#include "http/HeaderLimits.hxx"
#include "http/Method.hxx"
#include "http/HeaderParser.hxx"
#include "strmap.hxx"
#include "product.h"
#include "pool/pool.hxx"
#include "system/Error.hxx"
#include "net/BufferedSocketLease.hxx"
#include "net/SocketError.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/TimeoutError.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "util/DestructObserver.hxx"
#include "util/StringAPI.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <fmt/format.h>

#include <utility> // for std::unreachable()

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

bool
IsFcgiClientRetryFailure(std::exception_ptr error) noexcept
{
	if (FindNested<SocketClosedPrematurelyError>(error))
		return true;
	else if (const auto *e = FindNested<FcgiClientError>(error)) {
		switch (e->GetCode()) {
		case FcgiClientErrorCode::UNSPECIFIED:
		case FcgiClientErrorCode::GARBAGE:
			return false;

		case FcgiClientErrorCode::REFUSED:
		case FcgiClientErrorCode::IO:
			return true;
		}

		return false;
	} else
		return false;
}

class FcgiClient final
	: BufferedSocketHandler, Cancellable, Istream, IstreamSink,
	  FcgiFrameHandler,
	  DestructAnchor
{
	BufferedSocketLease socket;

	UniqueFileDescriptor stderr_fd;

	const StopwatchPtr stopwatch;

	HttpResponseHandler &handler;

	const uint16_t id;

	struct Request {
		/**
		 * This flag is set when the request istream has submitted
		 * data.  It is used to check whether the request istream is
		 * unavailable, to unschedule the socket write event.
		 */
		bool got_data;
	} request;

	struct Response {
		StringMap headers;

		std::size_t total_header_size = 0;

		off_t available;

		/**
		 * Only used when #no_body is set.
		 */
		HttpStatus status;

		/**
		 * This flag is true in HEAD requests.  HEAD responses may
		 * contain a Content-Length header, but no response body will
		 * follow (RFC 2616 4.3).
		 *
		 * With this flag, the contents of STDOUT packets are
		 * ignored and the response headers are only submitted
		 * after END_REQUEST was received.
		 */
		bool no_body;

		/**
		 * This flag is true if SubmitResponse() is currently calling
		 * the HTTP response handler.  During this period,
		 * fcgi_client_response_body_read() does nothing, to prevent
		 * recursion.
		 */
		bool in_handler;

		/**
		 * Are we currently inside _Read()?  We need to keep
		 * track of that to avoid invoking
		 * handler.OnIstreamReady() if the handler is
		 * currently invoking _Read().
		 */
		bool in_read;

		/**
		 * Are we currently receiving headers?  All STDOUT
		 * payloads will be fed to the header parser.
		 */
		bool receiving_headers = true;

		/**
		 * Is the FastCGI application currently sending a STDERR
		 * packet?
		 */
		bool stderr = false;

		/**
		 * Is the FastCGI application currently sending an
		 * END_REQUEST packet?  If yes, then we are waiting
		 * for its payload/padding to be received.
		 */
		bool end_request = false;

		explicit constexpr Response(bool _no_body) noexcept
			:no_body(_no_body) {}

		/**
		 * Were status and headers submitted to
		 * HttpResponseHandler::OnHttpResponse() already?
		 */
		constexpr bool WasResponseSubmitted() const noexcept {
			return !receiving_headers && !no_body;
		}
	} response;

	FcgiParser parser;

	/**
	 * How much STDERR content of the unconsumed input has already
	 * been handled?  We need to keep track of this because
	 * _FillBucketList() may be called multiple times without
	 * _ConsumeBucketList(), and only the first call shall handle
	 * STDERR content.
	 */
	std::size_t skip_stderr = 0;

public:
	FcgiClient(struct pool &_pool,
		   StopwatchPtr &&_stopwatch,
		   BufferedSocket &_socket, Lease &lease,
		   UniqueFileDescriptor &&_stderr_fd,
		   uint16_t _id, HttpMethod method,
		   UnusedIstreamPtr &&request_istream,
		   HttpResponseHandler &_handler,
		   CancellablePointer &cancel_ptr) noexcept;

	~FcgiClient() noexcept override;

	using Istream::GetPool;

	void Start() noexcept {
		socket.ScheduleRead();
		input.Read();
	}

private:
	/**
	 * Abort receiving the response status/headers from the FastCGI
	 * server, and notify the HTTP response handler.
	 */
	void AbortResponseHeaders(std::exception_ptr ep) noexcept;

	/**
	 * Abort receiving the response body from the FastCGI server, and
	 * notify the response body istream handler.
	 */
	void AbortResponseBody(std::exception_ptr ep) noexcept;

	/**
	 * Abort receiving the response from the FastCGI server.  This is
	 * a wrapper for AbortResponseHeaders() or AbortResponseBody().
	 */
	void AbortResponse(std::exception_ptr ep) noexcept;

	void HandleStderrPayload(std::span<const std::byte> payload) noexcept;

	/**
	 * Return type for AnalyseBuffer().
	 */
	struct BufferAnalysis {
		/**
		 * Offset of the end of the
		 * #FcgiRecordType::END_REQUEST packet, or 0 if none
		 * was found.
		 */
		std::size_t end_request_offset = 0;

		/**
		 * Amount of #FcgiRecordType::STDOUT data found in the
		 * buffer.
		 */
		std::size_t total_stdout = 0;
	};

	struct AnalysisHandler;
	struct FillBucketHandler;
	struct ConsumeBucketHandler;

private:
	/**
	 * Find the #FcgiRecordType::END_REQUEST packet matching the
	 * current request, and returns the offset where it ends, or 0
	 * if none was found.
	 */
	[[gnu::pure]]
	BufferAnalysis AnalyseBuffer(std::span<const std::byte> buffer) const noexcept;

	/**
	 * Throws on error.
	 */
	bool HandleLine(std::string_view line);

	/**
	 * Throws on error.
	 */
	std::size_t ParseHeaders(std::string_view src);

	/**
	 * Feed data into the FastCGI protocol parser.
	 *
	 * @return the number of bytes consumed, or 0 if this object has
	 * been destructed
	 */
	std::size_t Feed(std::span<const std::byte> src) noexcept;

	/**
	 * Submit the response metadata to the #HttpResponseHandler.
	 *
	 * @return false if the connection was closed
	 */
	bool SubmitResponse() noexcept;

	/**
	 * The END_REQUEST packet was received completely.  This
	 * function will always destroy the client.
	 */
	void HandleEndComplete() noexcept;

	/**
	 * Handle an END_REQUEST packet.
	 *
	 * @return false if the client has been destroyed
	 */
	FrameResult HandleEnd() noexcept;

	/**
	 * Consume data from the input buffer.
	 */
	BufferedResult ConsumeInput(std::span<const std::byte> src) noexcept;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	bool OnBufferedTimeout() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;
	void _Close() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	// virtual methods from class FcgiFrameHandler
	void OnFrameConsumed(std::size_t nbytes) noexcept override;
	FrameResult OnFrameHeader(FcgiRecordType type, uint_least16_t request_id) override;
	std::pair<FrameResult, std::size_t> OnFramePayload(std::span<const std::byte> src) override;
	FrameResult OnFrameEnd() override;
};

static constexpr auto fcgi_client_timeout = std::chrono::minutes{1};

inline FcgiClient::~FcgiClient() noexcept
{
	if (!socket.IsReleased())
		socket.Release(false, PutAction::DESTROY);
}

void
FcgiClient::AbortResponseHeaders(std::exception_ptr ep) noexcept
{
	assert(!response.WasResponseSubmitted());

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

void
FcgiClient::AbortResponseBody(std::exception_ptr ep) noexcept
{
	assert(response.WasResponseSubmitted());

	DestroyError(ep);
}

void
FcgiClient::AbortResponse(std::exception_ptr ep) noexcept
{
	if (!response.WasResponseSubmitted())
		AbortResponseHeaders(ep);
	else
		AbortResponseBody(ep);
}

void
FcgiClient::_Close() noexcept
{
	assert(response.WasResponseSubmitted());

	stopwatch.RecordEvent("close");

	Istream::_Close();
}

void
FcgiClient::HandleStderrPayload(std::span<const std::byte> payload) noexcept
{
	if (stderr_fd.IsDefined())
		stderr_fd.Write(payload);
	else
		fwrite(payload.data(), 1, payload.size(), stderr);
}

struct FcgiClient::AnalysisHandler final : FcgiFrameHandler {
	FcgiClient::BufferAnalysis result;
	std::size_t consumed = 0;
	const uint_least16_t id;

	bool is_end = false;

	explicit AnalysisHandler(uint_least16_t _id) noexcept
		:id(_id) {}

	// virtual methods from class FcgiFrameHandler
	void OnFrameConsumed(std::size_t nbytes) noexcept override {
		consumed += nbytes;
	}

	FrameResult OnFrameHeader(FcgiRecordType type, uint_least16_t request_id) override {
		assert(!is_end);

		if (request_id != id)
			/* ignore packets from other requests */
			return FrameResult::SKIP;

		switch (type) {
		case FcgiRecordType::STDOUT:
			return FrameResult::CONTINUE;

		case FcgiRecordType::END_REQUEST:
			/* found the END packet: stop here */
			is_end = true;
			return FrameResult::SKIP;

		default:
			return FrameResult::SKIP;
		}
	}

	std::pair<FrameResult, std::size_t> OnFramePayload(std::span<const std::byte> src) override {
		result.total_stdout += src.size();
		return {FrameResult::CONTINUE, src.size()};
	}

	FrameResult OnFrameEnd() override {
		if (is_end) {
			result.end_request_offset = consumed;
			return FrameResult::STOP;
		} else
			return FrameResult::CONTINUE;
	}
};

FcgiClient::BufferAnalysis
FcgiClient::AnalyseBuffer(const std::span<const std::byte> buffer) const noexcept
{
	AnalysisHandler analysis_handler{id};

	auto analysis_parser = parser;
	if (response.stderr)
		analysis_parser.SkipCurrent();

	analysis_parser.Feed(buffer, analysis_handler);
	return analysis_handler.result;
}

inline bool
FcgiClient::HandleLine(std::string_view line)
{
	assert(response.receiving_headers);
	assert(!response.stderr);

	if (!line.empty()) {
		if (line.size() >= MAX_HTTP_HEADER_SIZE)
			throw FcgiClientError{FcgiClientErrorCode::GARBAGE, "Response header is too long"};

		response.total_header_size += line.size();
		if (response.total_header_size >= MAX_TOTAL_HTTP_HEADER_SIZE)
			throw FcgiClientError{FcgiClientErrorCode::GARBAGE, "Too many response headers"};

		if (!header_parse_line(GetPool(), response.headers, line))
			throw FcgiClientError(FcgiClientErrorCode::GARBAGE, "Malformed FastCGI response header");
		return false;
	} else {
		stopwatch.RecordEvent("response_headers");

		response.receiving_headers = false;
		response.in_read = false;
		return true;
	}
}

inline std::size_t
FcgiClient::ParseHeaders(const std::string_view src0)
{
	std::string_view src = src0;
	const char *next = nullptr;

	while (true) {
		const auto [line, rest] = Split(src, '\n');
		next = rest.data();
		if (next == nullptr)
			break;

		if (HandleLine(StripRight(line)))
			break;

		src = rest;
	}

	return next != nullptr ? next - src0.data() : 0;
}

inline std::size_t
FcgiClient::Feed(std::span<const std::byte> src) noexcept
{
	if (response.stderr) {
		/* ignore errors and partial writes while forwarding STDERR
		   payload; there's nothing useful we can do, and we can't let
		   this delay/disturb the response delivery */

		const std::size_t consumed = src.size();

		if (src.size() > skip_stderr) {
			src = src.subspan(skip_stderr);
			skip_stderr = 0;
			HandleStderrPayload(src);
		} else {
			skip_stderr -= src.size();
		}

		return consumed;
	}

	if (response.receiving_headers) {
		try {
			return ParseHeaders(ToStringView(src));
		} catch (...) {
			AbortResponseHeaders(std::current_exception());
			return 0;
		}
	} else {
		assert(!response.no_body);
		assert(response.available < 0 ||
		       std::cmp_less_equal(src.size(), response.available));

		std::size_t consumed = InvokeData(src);
		if (consumed > 0 && response.available >= 0) {
			assert(std::cmp_less_equal(consumed, response.available));
			response.available -= consumed;
		}

		return consumed;
	}
}

inline bool
FcgiClient::SubmitResponse() noexcept
{
	assert(!response.receiving_headers);

	HttpStatus status = HttpStatus::OK;

	const char *p = response.headers.Remove(status_header);
	if (p != nullptr) {
		int i = atoi(p);
		if (http_status_is_valid(static_cast<HttpStatus>(i)))
			status = static_cast<HttpStatus>(i);
	}

	if (http_status_is_empty(status))
		response.no_body = true;

	if (response.no_body) {
		stopwatch.RecordEvent("response_no_body");

		response.status = status;

		/* ignore the rest of this STDOUT payload */
		parser.SkipCurrent();

		return true;
	}

	response.available = -1;
	p = response.headers.Remove(content_length_header);
	if (p != nullptr) {
		char *endptr;
		unsigned long long l = strtoull(p, &endptr, 10);
		if (endptr > p && *endptr == 0)
			response.available = l;
	}

	const DestructObserver destructed{*this};
	response.in_handler = true;
	handler.InvokeResponse(status, std::move(response.headers),
			       UnusedIstreamPtr(this));
	if (destructed)
		return false;

	response.in_handler = false;

	return true;
}

inline void
FcgiClient::HandleEndComplete() noexcept
{
	assert(!response.receiving_headers);
	assert(response.no_body || response.available == 0);

	if (response.no_body) {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeResponse(response.status, std::move(response.headers),
					UnusedIstreamPtr{});
	} else
		DestroyEof();
}

inline FcgiFrameHandler::FrameResult
FcgiClient::HandleEnd() noexcept
{
	assert(!response.end_request);
	response.end_request = true;

	stopwatch.RecordEvent("end");

	if (response.receiving_headers) {
		AbortResponseHeaders(std::make_exception_ptr(FcgiClientError(FcgiClientErrorCode::GARBAGE,
									     "premature end of headers "
									     "from FastCGI application")));
		return FrameResult::CLOSED;
	} else if (!response.no_body && response.available > 0) {
		AbortResponseBody(std::make_exception_ptr(FcgiClientError(FcgiClientErrorCode::GARBAGE,
									  "premature end of body "
									  "from FastCGI application")));
		return FrameResult::CLOSED;
	}

	response.available = 0;

	return FrameResult::SKIP;
}

void
FcgiClient::OnFrameConsumed(std::size_t nbytes) noexcept
{
	socket.DisposeConsumed(nbytes);
}

FcgiFrameHandler::FrameResult
FcgiClient::OnFrameHeader(FcgiRecordType type, uint_least16_t request_id)
{
	if (request_id != id)
		/* wrong request id; discard this packet */
		return FrameResult::SKIP;

	switch (type) {
	case FcgiRecordType::STDOUT:
		response.stderr = false;

		if (!response.receiving_headers && response.no_body)
			/* ignore all payloads until END_REQUEST */
			return FrameResult::SKIP;

		return FrameResult::CONTINUE;

	case FcgiRecordType::STDERR:
		response.stderr = true;
		return FrameResult::CONTINUE;

	case FcgiRecordType::END_REQUEST:
		return HandleEnd();

	default:
		return FrameResult::SKIP;
	}

}

std::pair<FcgiFrameHandler::FrameResult, std::size_t>
FcgiClient::OnFramePayload(std::span<const std::byte> payload)
{
	const bool at_headers = response.receiving_headers;

	if (response.WasResponseSubmitted() &&
	    !response.stderr &&
	    response.available >= 0 &&
	    std::cmp_greater(payload.size(), response.available)) {
		/* the DATA packet was larger than the Content-Length
		   declaration - fail */
		AbortResponseBody(std::make_exception_ptr(FcgiClientError(FcgiClientErrorCode::GARBAGE,
									  "excess data at end of body "
									  "from FastCGI application")));
		return {FrameResult::CLOSED, 0};
	}

	const DestructObserver destructed{*this};
	std::size_t nbytes = Feed(payload);
	if (nbytes == 0) {
		if (destructed)
			return {FrameResult::CLOSED, 0};

		if (at_headers) {
			/* incomplete header line received, want more
			   data */
			assert(response.receiving_headers);
			return {FrameResult::CONTINUE, 0};
		}

		/* the response body handler blocks, wait for it to
		   become ready */
		return {FrameResult::CONTINUE, 0};
	}

	if (at_headers && !response.receiving_headers) {
		/* the read_state has been switched from HEADERS to
		   BODY: we have to deliver the response now */

		return {FrameResult::STOP, nbytes};
	}

	return {FrameResult::CONTINUE, nbytes};
}

FcgiFrameHandler::FrameResult
FcgiClient::OnFrameEnd()
{
	if (response.end_request) {
		HandleEndComplete();
		return FrameResult::CLOSED;
	} else
		return FrameResult::CONTINUE;
}

inline BufferedResult
FcgiClient::ConsumeInput(std::span<const std::byte> src) noexcept
{
	assert(!src.empty());

	const bool at_headers = response.receiving_headers;
	const auto result = parser.Feed(src, *this);
	if (result != FcgiParser::FeedResult::CLOSED)
		socket.AfterConsumed();

	switch (result) {
	case FcgiParser::FeedResult::OK:
		return BufferedResult::MORE;

	case FcgiParser::FeedResult::BLOCKING:
		return BufferedResult::OK;

	case FcgiParser::FeedResult::MORE:
		return BufferedResult::MORE;

	case FcgiParser::FeedResult::STOP:
		if (at_headers && !response.receiving_headers) {
			/* the read_state has been switched from HEADERS to
			   BODY: we have to deliver the response now */

			return SubmitResponse()
				/* continue parsing the response body from the
				   buffer */
				? BufferedResult::AGAIN
				: BufferedResult::DESTROYED;
		}

		return BufferedResult::OK;

	case FcgiParser::FeedResult::CLOSED:
		return BufferedResult::DESTROYED;
	}

	std::unreachable();
}

/*
 * istream handler for the request
 *
 */

std::size_t
FcgiClient::OnData(std::span<const std::byte> src) noexcept
{
	assert(socket.IsConnected());
	assert(HasInput());

	request.got_data = true;

	ssize_t nbytes = socket.Write(src);
	if (nbytes > 0) [[likely]] {
		if (static_cast<std::size_t>(nbytes) < src.size())
			socket.ScheduleWrite();
		else
			socket.DeferNextWrite();
	} else if (nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED) [[likely]]
		return 0;
	else if (nbytes < 0) {
		AbortResponse(NestException(std::make_exception_ptr(MakeSocketError("Write error")),
					    FcgiClientError(FcgiClientErrorCode::IO,
							    "write to FastCGI application failed")));
		return 0;
	}

	return (std::size_t)nbytes;
}

IstreamDirectResult
FcgiClient::OnDirect(FdType type, FileDescriptor fd, off_t offset,
		     std::size_t max_length, bool then_eof) noexcept
{
	assert(socket.IsConnected());

	request.got_data = true;

	ssize_t nbytes = socket.WriteFrom(fd, type, ToOffsetPointer(offset),
					  max_length);
	if (nbytes > 0) [[likely]] {
		input.ConsumeDirect(nbytes);

		if (then_eof && static_cast<std::size_t>(nbytes) == max_length) {
			stopwatch.RecordEvent("request_end");

			CloseInput();
			socket.UnscheduleWrite();
			return IstreamDirectResult::CLOSED;
		}

		socket.ScheduleWrite();
		return IstreamDirectResult::OK;
	} else if (nbytes == WRITE_BLOCKING)
		return IstreamDirectResult::BLOCKING;
	else if (nbytes == WRITE_DESTROYED)
		return IstreamDirectResult::CLOSED;
	else if (nbytes == WRITE_SOURCE_EOF)
		return IstreamDirectResult::END;
	else {
		if (errno == EAGAIN) {
			request.got_data = false;
			socket.UnscheduleWrite();
		}

		return IstreamDirectResult::ERRNO;
	}
}

void
FcgiClient::OnEof() noexcept
{
	assert(HasInput());
	ClearInput();

	stopwatch.RecordEvent("request_end");

	socket.UnscheduleWrite();
}

void
FcgiClient::OnError(std::exception_ptr ep) noexcept
{
	assert(HasInput());
	ClearInput();

	stopwatch.RecordEvent("request_error");

	AbortResponse(NestException(ep,
				    std::runtime_error("FastCGI request stream failed")));
}

/*
 * istream implementation for the response body
 *
 */

off_t
FcgiClient::_GetAvailable(bool partial) noexcept
{
	if (response.available >= 0)
		return response.available;

	const std::size_t remaining = parser.GetRemaining();
	const auto buffer = socket.ReadBuffer();
	if (buffer.size() > remaining) {
		const auto analysis = AnalyseBuffer(buffer);
		if (analysis.end_request_offset > 0 || partial)
			return analysis.total_stdout;
	}

	return partial && !response.stderr ? (off_t)remaining : -1;
}

void
FcgiClient::_Read() noexcept
{
	assert(response.WasResponseSubmitted());
	assert(!response.in_read);

	if (response.in_handler)
		/* avoid recursion; the http_response_handler caller will
		   continue parsing the response if possible */
		return;

	response.in_read = true;

	if (socket.Read() == BufferedReadResult::DESTROYED)
		return;

	response.in_read = false;
}

struct FcgiClient::FillBucketHandler final : FcgiFrameHandler {
	FcgiClient &client;

	IstreamBucketList &list;

	off_t available = client.response.available;

	std::size_t total_size = 0;

	std::size_t current_skip_stderr = client.skip_stderr;

	bool current_stderr = client.response.stderr;
	bool found_end_request = client.response.end_request;
	bool release_socket = false;

	PutAction put_action;

	FillBucketHandler(FcgiClient &_client, IstreamBucketList &_list) noexcept
		:client(_client), list(_list) {}

	// virtual methods from class FcgiFrameHandler
	FrameResult OnFrameHeader(FcgiRecordType type, uint_least16_t request_id) override;
	std::pair<FrameResult, std::size_t> OnFramePayload(std::span<const std::byte> src) override;
	FrameResult OnFrameEnd() override;
};

FcgiFrameHandler::FrameResult
FcgiClient::FillBucketHandler::OnFrameHeader(FcgiRecordType type, uint_least16_t request_id)
{
	if (request_id != client.id)
		/* ignore packets from other requests */
		return FrameResult::SKIP;

	if (found_end_request) {
		/* just in case the END frame was already processed:
		   if we see more frames, the socket cannot be
		   reused */
		put_action = PutAction::DESTROY;
		return FrameResult::STOP;
	}

	switch (type) {
	case FcgiRecordType::STDOUT:
		current_stderr = false;
		return FrameResult::CONTINUE;

	case FcgiRecordType::STDERR:
		current_stderr = true;
		return FrameResult::CONTINUE;

	case FcgiRecordType::END_REQUEST:
		/* "excess data" has already been checked */
		assert(client.response.available < 0 ||
						   static_cast<std::size_t>(client.response.available) >= total_size);

		if (available > 0) {
			throw FcgiClientError(FcgiClientErrorCode::GARBAGE,
					      "premature end of body "
					      "from FastCGI application");
		} else if (client.response.available < 0) {
			/* now we know how much data remains */
			client.response.available = total_size;
		}

		found_end_request = true;
		return FrameResult::SKIP;

	default:
		/* ignore unknown packets */
		return FrameResult::SKIP;
	}
}

std::pair<FcgiFrameHandler::FrameResult, std::size_t>
FcgiClient::FillBucketHandler::OnFramePayload(std::span<const std::byte> src)
{
	const std::size_t consumed = src.size();

	if (current_stderr) {
		if (src.size() > current_skip_stderr) {
			src = src.subspan(current_skip_stderr);
			current_skip_stderr = 0;
			client.HandleStderrPayload(src);
			client.skip_stderr += src.size();
		} else {
			current_skip_stderr -= src.size();
		}
	} else {
		if (available >= 0) {
			if (std::cmp_greater(src.size(), available))
				/* the DATA packet was larger than the Content-Length
				   declaration - fail */
				throw FcgiClientError(FcgiClientErrorCode::GARBAGE,
						      "excess data at end of body "
						      "from FastCGI application");

			available -= src.size();
		}

		list.Push(src);
		total_size += src.size();
	}

	return {FrameResult::CONTINUE, consumed};
}

FcgiFrameHandler::FrameResult
FcgiClient::FillBucketHandler::OnFrameEnd()
{
	if (found_end_request) {
		release_socket = true;
		put_action = PutAction::REUSE;
		return FrameResult::STOP;
	} else
		return FrameResult::CONTINUE;
}

void
FcgiClient::_FillBucketList(IstreamBucketList &list)
{
	assert(response.WasResponseSubmitted());

	if (response.available == 0 && socket.IsReleased())
		return;

	FillBucketHandler fill_bucket_handler{*this, list};

	auto fill_bucket_parser = parser;

	try {
		fill_bucket_parser.Feed(socket.ReadBuffer(), fill_bucket_handler);
	} catch (...) {
		Destroy();
		throw;
	}

	if (fill_bucket_handler.release_socket && !socket.IsReleased())
		socket.Release(true, fill_bucket_handler.put_action);

	/* report EOF only after we have received the whole
	   END_REQUEST payload/padding */
	if (!socket.IsReleased())
		list.SetMore();
}

struct FcgiClient::ConsumeBucketHandler final : FcgiFrameHandler {
	FcgiClient &client;

	std::size_t nbytes, total = 0;

	ConsumeBucketHandler(FcgiClient &_client, std::size_t _nbytes) noexcept
		:client(_client), nbytes(_nbytes) {}

	// virtual methods from class FcgiFrameHandler
	void OnFrameConsumed(std::size_t n) noexcept override;
	FrameResult OnFrameHeader(FcgiRecordType type, uint_least16_t request_id) override;
	std::pair<FrameResult, std::size_t> OnFramePayload(std::span<const std::byte> src) override;
	FrameResult OnFrameEnd() override;
};

void
FcgiClient::ConsumeBucketHandler::OnFrameConsumed(std::size_t n) noexcept
{
	client.socket.DisposeConsumed(n);
}

FcgiFrameHandler::FrameResult
FcgiClient::ConsumeBucketHandler::OnFrameHeader(FcgiRecordType type, uint_least16_t request_id)
{
	if (request_id != client.id)
		/* ignore packets from other requests */
		return FrameResult::SKIP;

	switch (type) {
	case FcgiRecordType::END_REQUEST:
		if (client.socket.IsReleased()) {
			/* this condition must have been detected
			   already in _FillBucketList() */
			assert(client.response.available == 0);

			client.response.end_request = true;
		}

		return FrameResult::SKIP;

	case FcgiRecordType::STDOUT:
		client.response.stderr = false;
		return FrameResult::CONTINUE;

	case FcgiRecordType::STDERR:
		client.response.stderr = true;
		return FrameResult::CONTINUE;

	default:
		/* ignore unknown packets */
		return FrameResult::SKIP;
	}
}

std::pair<FcgiFrameHandler::FrameResult, std::size_t>
FcgiClient::ConsumeBucketHandler::OnFramePayload(std::span<const std::byte> src)
{
	FrameResult result = FrameResult::CONTINUE;
	std::size_t consumed = src.size();

	if (client.response.stderr) {
		assert(consumed <= client.skip_stderr);
		client.skip_stderr -= consumed;
	} else {
		if (nbytes < consumed) {
			consumed = nbytes;
			result = FrameResult::STOP;
		}

		nbytes -= consumed;
		total += consumed;

		if (client.response.available > 0)
			client.response.available -= consumed;
	}

	return {result, consumed};
}

FcgiFrameHandler::FrameResult
FcgiClient::ConsumeBucketHandler::OnFrameEnd()
{
	return FrameResult::CONTINUE;
}

Istream::ConsumeBucketResult
FcgiClient::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	assert(response.available != 0);
	assert(response.WasResponseSubmitted());

	ConsumeBucketHandler consume_bucket_handler{*this, nbytes};

	parser.Feed(socket.ReadBuffer(), consume_bucket_handler);

	socket.AfterConsumed();

	assert(response.end_request || consume_bucket_handler.nbytes == 0);

	if (consume_bucket_handler.total > 0 && !response.end_request && socket.IsConnected())
		socket.ScheduleRead();

	return {Consumed(consume_bucket_handler.total), response.end_request};
}

/*
 * socket_wrapper handler
 *
 */

BufferedResult
FcgiClient::OnBufferedData()
{
	if (response.WasResponseSubmitted() && !response.in_read) {
		switch (InvokeReady()) {
		case IstreamReadyResult::OK:
			return BufferedResult::OK;

		case IstreamReadyResult::FALLBACK:
			break;

		case IstreamReadyResult::CLOSED:
			return BufferedResult::DESTROYED;
		}
	}

	auto r = socket.ReadBuffer();
	assert(!r.empty());

	if (!socket.IsReleased()) {
		/* check if the END_REQUEST packet can be found in the
		   following data chunk */
		const auto analysis = AnalyseBuffer(r);
		if (analysis.end_request_offset > 0 &&
		    analysis.end_request_offset <= r.size())
			/* found it: we no longer need the socket, everything we
			   need is already in the given buffer */
			socket.Release(true,
				       analysis.end_request_offset == r.size()
				       ? PutAction::REUSE
				       : PutAction::DESTROY);
	}

	return ConsumeInput(r);
}

bool
FcgiClient::OnBufferedClosed() noexcept
{
	stopwatch.RecordEvent("socket_closed");

	/* the rest of the response may already be in the input buffer */
	socket.Release(false, PutAction::DESTROY);
	return true;
}

bool
FcgiClient::OnBufferedWrite()
{
	const DestructObserver destructed(*this);

	request.got_data = false;
	input.Read();

	const bool result = !destructed;
	if (result && HasInput()) {
		if (request.got_data)
			socket.ScheduleWrite();
		else
			socket.UnscheduleWrite();
	}

	return result;
}

bool
FcgiClient::OnBufferedTimeout() noexcept
{
	stopwatch.RecordEvent("timeout");

	AbortResponse(std::make_exception_ptr(TimeoutError{}));
	return false;
}

void
FcgiClient::OnBufferedError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("socket_error");

	AbortResponse(NestException(ep, FcgiClientError(FcgiClientErrorCode::IO, "FastCGI socket error")));
}

/*
 * async operation
 *
 */

void
FcgiClient::Cancel() noexcept
{
	/* Cancellable::Cancel() can only be used before the
	   response was delivered to our callback */
	assert(!response.WasResponseSubmitted());
	assert(!socket.IsReleased());

	stopwatch.RecordEvent("cancel");

	Destroy();
}

/*
 * constructor
 *
 */

inline
FcgiClient::FcgiClient(struct pool &_pool,
		       StopwatchPtr &&_stopwatch,
		       BufferedSocket &_socket, Lease &lease,
		       UniqueFileDescriptor &&_stderr_fd,
		       uint16_t _id, HttpMethod method,
		       UnusedIstreamPtr &&request_istream,
		       HttpResponseHandler &_handler,
		       CancellablePointer &cancel_ptr) noexcept
	:Istream(_pool),
	 IstreamSink(std::move(request_istream)),
	 socket(_socket, lease, fcgi_client_timeout, *this),
	 stderr_fd(std::move(_stderr_fd)),
	 stopwatch(std::move(_stopwatch)),
	 handler(_handler),
	 id(_id),
	 response(http_method_is_empty(method))
{
	input.SetDirect(istream_direct_mask_to(socket.GetType()));

	cancel_ptr = *this;
}

void
fcgi_client_request(struct pool *pool,
		    StopwatchPtr stopwatch,
		    BufferedSocket &socket, Lease &lease,
		    HttpMethod method, const char *uri,
		    const char *script_filename,
		    const char *script_name, const char *path_info,
		    const char *query_string,
		    const char *document_root,
		    const char *remote_addr,
		    const StringMap &headers, UnusedIstreamPtr body,
		    std::span<const char *const> params,
		    UniqueFileDescriptor &&stderr_fd,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept
{
	static unsigned next_request_id = 1;
	++next_request_id;

	FcgiRecordHeader header{
		.version = FCGI_VERSION_1,
		.type = FcgiRecordType::BEGIN_REQUEST,
		.request_id = next_request_id,
	};
	static constexpr FcgiBeginRequest begin_request{
		.role = static_cast<uint16_t>(FcgiRole::RESPONDER),
		.flags = FCGI_FLAG_KEEP_CONN,
	};

	assert(http_method_is_valid(method));

	GrowingBuffer buffer;
	header.content_length = sizeof(begin_request);
	buffer.WriteT(header);
	buffer.WriteT(begin_request);

	FcgiParamsSerializer ps(buffer, header.request_id);

	ps("REQUEST_METHOD", http_method_to_string(method))
		("REQUEST_URI", uri)
		("SCRIPT_FILENAME", script_filename)
		("SCRIPT_NAME", script_name)
		("PATH_INFO", path_info)
		("QUERY_STRING", query_string)
		("DOCUMENT_ROOT", document_root)
		("SERVER_SOFTWARE", PRODUCT_TOKEN);

	if (remote_addr != nullptr)
		ps("REMOTE_ADDR", remote_addr);

	off_t available = body
		? body.GetAvailable(false)
		: -1;
	if (available >= 0) {
		const fmt::format_int value{available};

		ps("HTTP_CONTENT_LENGTH", value.c_str())
			/* PHP wants the parameter without
			   "HTTP_" */
			("CONTENT_LENGTH", value.c_str());
	}

	if (const char *content_type = headers.Get(content_type_header);
	    content_type != nullptr)
		/* same for the "Content-Type" request
		   header */
		ps("CONTENT_TYPE", content_type);

	if (const char *https = headers.Get(x_cm4all_https_header);
	    https != nullptr && StringIsEqual(https, "on"))
		ps("HTTPS", https);

	ps.Headers(headers);

	for (const std::string_view param : params) {
		const auto [name, value] = Split(param, '=');
		if (!name.empty() && value.data() != nullptr)
			ps(name, value);
	}

	ps.Commit();

	header.type = FcgiRecordType::PARAMS;
	header.content_length = 0;
	buffer.WriteT(header);

	UnusedIstreamPtr request;

	if (body)
		/* format the request body */
		request = NewConcatIstream(*pool,
					   istream_gb_new(*pool, std::move(buffer)),
					   istream_fcgi_new(*pool, std::move(body),
							    header.request_id));
	else {
		/* no request body - append an empty STDIN packet */
		header.type = FcgiRecordType::STDIN;
		header.content_length = 0;
		buffer.WriteT(header);

		request = istream_gb_new(*pool, std::move(buffer));
	}

	auto client = NewFromPool<FcgiClient>(*pool, *pool,
					      std::move(stopwatch),
					      socket, lease,
					      std::move(stderr_fd),
					      header.request_id, method,
					      std::move(request),
					      handler, cancel_ptr);
	client->Start();
}
