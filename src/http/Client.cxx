/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Client.hxx"
#include "Body.hxx"
#include "Upgrade.hxx"
#include "ResponseHandler.hxx"
#include "HeaderParser.hxx"
#include "HeaderWriter.hxx"
#include "http/List.hxx"
#include "istream/Bucket.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/OptionalIstream.hxx"
#include "istream/ChunkedIstream.hxx"
#include "istream/DechunkIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_null.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/istream_gb.hxx"
#include "uri/Verify.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"
#include "fs/Lease.hxx"
#include "AllocatorPtr.hxx"
#include "system/Error.hxx"
#include "io/Iovec.hxx"
#include "io/Logger.hxx"
#include "io/SpliceSupport.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"
#include "util/DestructObserver.hxx"
#include "util/StringStrip.hxx"
#include "util/StringView.hxx"
#include "util/StringFormat.hxx"
#include "util/StaticVector.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"
#include "util/Compiler.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

bool
IsHttpClientServerFailure(std::exception_ptr ep) noexcept
{
	const auto *e = FindNested<HttpClientError>(ep);
	return e != nullptr &&
		e->GetCode() != HttpClientErrorCode::UNSPECIFIED;
}

bool
IsHttpClientRetryFailure(std::exception_ptr ep) noexcept
{
	const auto *e = FindNested<HttpClientError>(ep);
	if (e == nullptr)
		return false;

	switch (e->GetCode()) {
	case HttpClientErrorCode::UNSPECIFIED:
		return false;

	case HttpClientErrorCode::REFUSED:
	case HttpClientErrorCode::PREMATURE:
	case HttpClientErrorCode::IO:
	case HttpClientErrorCode::GARBAGE:
		return true;
	}

	return false;
}

/**
 * With a request body of this size or larger, we send "Expect:
 * 100-continue".
 */
static constexpr off_t EXPECT_100_THRESHOLD = 1024;

static constexpr auto http_client_timeout = std::chrono::seconds(30);

class HttpClient final : BufferedSocketHandler, IstreamSink, Cancellable, DestructAnchor, PoolLeakDetector {
	enum class BucketResult {
		MORE,
		BLOCKING,
		DEPLETED,
		DESTROYED,
	};

	struct ResponseBodyReader final : HttpBodyReader {
		template<typename P>
		explicit ResponseBodyReader(P &&_pool) noexcept
			:HttpBodyReader(std::forward<P>(_pool)) {}

		HttpClient &GetClient() noexcept {
			return ContainerCast(*this, &HttpClient::response_body_reader);
		}

		/* virtual methods from class Istream */

		off_t _GetAvailable(bool partial) noexcept override {
			return GetClient().GetAvailable(partial);
		}

		void _Read() noexcept override {
			GetClient().Read();
		}

		void _FillBucketList(IstreamBucketList &list) override {
			GetClient().FillBucketList(list);
		}

		std::size_t _ConsumeBucketList(std::size_t nbytes) noexcept override {
			return GetClient().ConsumeBucketList(nbytes);
		}

		int _AsFd() noexcept override {
			return GetClient().AsFD();
		}

		void _Close() noexcept override {
			GetClient().Close();
		}
	};

	struct pool &pool;
	struct pool &caller_pool;

	const char *const peer_name;
	const StopwatchPtr stopwatch;

	EventLoop &event_loop;

	/* I/O */
	FilteredSocketLease socket;

	/* request */
	struct Request {
		/**
		 * This #OptionalIstream blocks sending the request body until
		 * the server has confirmed "100 Continue".
		 */
		SharedPoolPtr<OptionalIstreamControl> pending_body;

		char content_length_buffer[32];

		/**
		 * This flag is set when the request istream has submitted
		 * data.  It is used to check whether the request istream is
		 * unavailable, to unschedule the socket write event.
		 */
		bool got_data;

		HttpResponseHandler &handler;

		explicit Request(HttpResponseHandler &_handler) noexcept
			:handler(_handler) {}
	} request;

	/* response */
	struct Response {
		enum class State : uint8_t {
			STATUS,
			HEADERS,
			BODY,
			END,
		} state;

		/**
		 * This flag is true in HEAD requests.  HEAD responses may
		 * contain a Content-Length header, but no response body will
		 * follow (RFC 2616 4.3).
		 */
		bool no_body;

		/**
		 * This flag is true if we are currently calling the HTTP
		 * response handler.  During this period,
		 * http_client_response_stream_read() does nothing, to prevent
		 * recursion.
		 */
		bool in_handler;

		http_status_t status;
		StringMap headers;

		/**
		 * The response body pending to be submitted to the
		 * #HttpResponseHandler.
		 */
		UnusedIstreamPtr body;
	} response;

	ResponseBodyReader response_body_reader;

	/* connection settings */
	bool keep_alive;

public:
	HttpClient(struct pool &_pool, struct pool &_caller_pool,
		   StopwatchPtr &&_stopwatch,
		   FilteredSocket &_socket, Lease &lease,
		   const char *_peer_name,
		   http_method_t method, const char *uri,
		   const StringMap &headers,
		   GrowingBuffer &&more_headers,
		   UnusedIstreamPtr body, bool expect_100,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;

	~HttpClient() noexcept {
		if (!socket.IsReleased())
			ReleaseSocket(false, false);
	}

private:
	struct pool &GetPool() const noexcept {
		return pool;
	}

	/**
	 * @return false if the #HttpClient has released the socket
	 */
	[[gnu::pure]]
	bool IsConnected() const noexcept {
		return socket.IsConnected();
	}

	[[gnu::pure]]
	bool CheckDirect() const noexcept {
		assert(socket.GetType() == FdType::FD_NONE || IsConnected());
		assert(response.state == Response::State::BODY);

		return response_body_reader.CheckDirect(socket.GetType());
	}

	void DeferWrite() noexcept {
		assert(IsConnected());

		socket.DeferWrite();
	}

	void ScheduleWrite() noexcept {
		assert(IsConnected());

		socket.ScheduleWrite();
	}

	/**
	 * Release the socket held by this object.
	 */
	void ReleaseSocket(bool preserve, bool reuse) noexcept {
		assert(!socket.IsReleased());

		if (HasInput()) {
			/* the request body is still being
			   transferred */
			CloseInput();

			/* closing a partially transferred request
			   body means the HTTP connection is dirty, so
			   we need to disable keep-alive */
			reuse = false;
		}

		socket.Release(preserve, reuse);
	}

	void Destroy() noexcept {
		/* this pool reference ensures that our destructor can finish
		   execution even if HttpBodyReader's reference is released in
		   its destructor (which is called from within our
		   destructor) */
		const ScopePoolRef ref(pool);

		DeleteFromPool(pool, this);
	}

	void DestroyInvokeError(std::exception_ptr ep) noexcept {
		auto &_handler = request.handler;
		Destroy();
		_handler.InvokeError(ep);
	}

	std::exception_ptr PrefixError(std::exception_ptr ep) const noexcept {
		return NestException(ep,
				     FormatRuntimeError("error on HTTP connection to '%s'",
							peer_name));
	}

	void AbortResponseHeaders(std::exception_ptr ep) noexcept;
	void AbortResponseBody(std::exception_ptr ep) noexcept;
	void AbortResponse(std::exception_ptr ep) noexcept;

	void AbortResponseHeaders(HttpClientErrorCode code, const char *msg) noexcept {
		AbortResponseHeaders(std::make_exception_ptr(HttpClientError(code, msg)));
	}

	void AbortResponse(HttpClientErrorCode code, const char *msg) noexcept {
		AbortResponse(std::make_exception_ptr(HttpClientError(code, msg)));
	}

	void ResponseFinished() noexcept;

	[[gnu::pure]]
	off_t GetAvailable(bool partial) const noexcept;

	void Read() noexcept;

	void FillBucketList(IstreamBucketList &list) noexcept;
	std::size_t ConsumeBucketList(std::size_t nbytes) noexcept;

	int AsFD() noexcept;
	void Close() noexcept;

	BucketResult TryWriteBuckets2();
	BucketResult TryWriteBuckets();

	/**
	 * Throws on error.
	 */
	void ParseStatusLine(StringView line);

	/**
	 * Throws on error.
	 */
	void HeadersFinished();

	/**
	 * Throws on error.
	 */
	void HandleLine(StringView line);

	/**
	 * Throws on error.
	 */
	BufferedResult ParseHeaders(StringView b);

	/**
	 * Throws on error.
	 */
	BufferedResult FeedHeaders(std::span<const std::byte> b);

	void ResponseBodyEOF() noexcept;

	BufferedResult FeedBody(std::span<const std::byte> b);

	DirectResult TryResponseDirect(SocketDescriptor fd, FdType fd_type);

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedRemaining(std::size_t remaining) noexcept override;
	bool OnBufferedWrite() override;
	enum write_result OnBufferedBroken() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(const void *data, std::size_t length) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseHeaders(std::exception_ptr ep) noexcept
{
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS);

	/* need to call PrefixError() before ReleaseSocket() because
	   the former uses the "peer_name" field which points to
	   memory owned by the socket */
	ep = PrefixError(std::move(ep));

	if (IsConnected())
		ReleaseSocket(false, false);

	DestroyInvokeError(std::move(ep));
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseBody(std::exception_ptr ep) noexcept
{
	assert(response.state == Response::State::BODY);

	if (HasInput())
		CloseInput();

	if (response_body_reader.GotEndChunk()) {
		/* avoid recursing from DechunkIstream: when DechunkIstream
		   reports EOF, and that handler closes the HttpClient, which
		   then destroys HttpBodyReader, which finally destroys
		   DechunkIstream ... */
	} else {
		response_body_reader.InvokeError(PrefixError(ep));
	}

	Destroy();
}

/**
 * Abort receiving the response status/headers/body from the HTTP
 * server.
 */
void
HttpClient::AbortResponse(std::exception_ptr ep) noexcept
{
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS ||
	       response.state == Response::State::BODY);

	if (response.state != Response::State::BODY)
		AbortResponseHeaders(ep);
	else
		AbortResponseBody(ep);
}


/*
 * istream implementation for the response body
 *
 */

inline off_t
HttpClient::GetAvailable(bool partial) const noexcept
{
	assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
	assert(response.state == Response::State::BODY);

	return response_body_reader.GetAvailable(socket, partial);
}

inline void
HttpClient::Read() noexcept
{
	assert(response_body_reader.IsSocketDone(socket) ||
	       /* the following condition avoids calling
		  FilteredSocketLease::HasEnded() when it would
		  assert-fail; this can happen if the socket has been
		  disconnected while there was still pending data, but our
		  handler had been blocking it; in that case,
		  HttpBodyReader::SocketEOF() leaves handling this
		  condition to the dechunker, which however is never
		  called while the handler blocks */
	       (response_body_reader.IsChunked() && !IsConnected()) ||
	       !socket.HasEnded());
	assert(response.state == Response::State::BODY);
	assert(response_body_reader.HasHandler());

	if (response_body_reader.IsEOF()) {
		/* just in case EOF has been reached by ConsumeBucketList() */
		ResponseBodyEOF();
		return;
	}

	if (IsConnected())
		socket.SetDirect(CheckDirect());

	if (response.in_handler)
		/* avoid recursion; the http_response_handler caller will
		   continue parsing the response if possible */
		return;

	socket.Read(response_body_reader.RequireMore());
}

inline void
HttpClient::FillBucketList(IstreamBucketList &list) noexcept
{
	assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
	assert(response.state == Response::State::BODY);

	response_body_reader.FillBucketList(socket, list);
}

inline std::size_t
HttpClient::ConsumeBucketList(std::size_t nbytes) noexcept
{
	assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
	assert(response.state == Response::State::BODY);

	return response_body_reader.ConsumeBucketList(socket, nbytes);
}

inline int
HttpClient::AsFD() noexcept
{
	assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
	assert(response.state == Response::State::BODY);

	if (!IsConnected() || !socket.IsEmpty() || socket.HasFilter() ||
	    keep_alive ||
	    /* must not be chunked */
	    response_body_reader.IsChunked())
		return -1;

	int fd = socket.AsFD();
	if (fd < 0)
		return -1;

	Destroy();
	return fd;
}

inline void
HttpClient::Close() noexcept
{
	assert(response.state == Response::State::BODY);

	stopwatch.RecordEvent("close");

	Destroy();
}

namespace {
struct RequestBodyCanceled {};
}

inline HttpClient::BucketResult
HttpClient::TryWriteBuckets2()
{
	if (socket.HasFilter())
		return BucketResult::MORE;

	IstreamBucketList list;
	input.FillBucketList(list);

	StaticVector<struct iovec, 64> v;
	for (const auto &bucket : list) {
		if (!bucket.IsBuffer())
			break;

		v.push_back(MakeIovec(bucket.GetBuffer()));

		if (v.full())
			break;
	}

	if (v.empty()) {
		bool has_more = list.HasMore();
		return has_more
			? BucketResult::MORE
			: BucketResult::DEPLETED;
	}

	ssize_t nbytes = socket.WriteV(v);
	if (nbytes < 0) {
		if (nbytes == WRITE_BLOCKING)
			[[likely]]
			return BucketResult::BLOCKING;

		if (nbytes == WRITE_DESTROYED)
			return BucketResult::DESTROYED;

		if (nbytes == WRITE_BROKEN)
			/* our input has already been closed by
			   OnBufferedBroken() */
			throw RequestBodyCanceled{};

		const int _errno = errno;

		throw HttpClientError(HttpClientErrorCode::IO,
				      StringFormat<64>("write error (%s)",
						       strerror(_errno)));
	}

	std::size_t consumed = input.ConsumeBucketList(nbytes);
	assert(consumed == (std::size_t)nbytes);

	return list.IsDepleted(consumed)
		? BucketResult::DEPLETED
		: BucketResult::MORE;
}

HttpClient::BucketResult
HttpClient::TryWriteBuckets()
{
	BucketResult result;

	try {
		result = TryWriteBuckets2();
	} catch (RequestBodyCanceled) {
		assert(!HasInput());
		stopwatch.RecordEvent("request_canceled");
		return BucketResult::DEPLETED;
	} catch (...) {
		assert(!HasInput());
		stopwatch.RecordEvent("send_error");
		AbortResponse(std::current_exception());
		return BucketResult::DESTROYED;
	}

	switch (result) {
	case BucketResult::MORE:
		assert(HasInput());
		break;

	case BucketResult::BLOCKING:
		assert(HasInput());
		ScheduleWrite();
		break;

	case BucketResult::DEPLETED:
		assert(HasInput());
		assert(!request.pending_body);

		stopwatch.RecordEvent("request_end");
		CloseInput();
		socket.ScheduleReadNoTimeout(true);
		break;

	case BucketResult::DESTROYED:
		break;
	}

	return result;
}

inline void
HttpClient::ParseStatusLine(StringView line)
{
	assert(response.state == Response::State::STATUS);

	const char *space;
	if (!line.SkipPrefix("HTTP/") || (space = line.Find(' ')) == nullptr) {
		stopwatch.RecordEvent("malformed");

		throw HttpClientError(HttpClientErrorCode::GARBAGE,
				      "malformed HTTP status line");
	}

	line.MoveFront(space + 1);

	if (line.size < 3 || !IsDigitASCII(line[0]) ||
	    !IsDigitASCII(line[1]) || !IsDigitASCII(line[2])) [[unlikely]] {
		stopwatch.RecordEvent("malformed");

		throw HttpClientError(HttpClientErrorCode::GARBAGE,
				      "no HTTP status found");
	}

	response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
	if (!http_status_is_valid(response.status)) [[unlikely]] {
		stopwatch.RecordEvent("malformed");

		throw HttpClientError(HttpClientErrorCode::GARBAGE,
				      StringFormat<64>("invalid HTTP status %d",
						       response.status));
	}

	response.state = Response::State::HEADERS;
}

inline void
HttpClient::HeadersFinished()
{
	stopwatch.RecordEvent("headers");

	auto &response_headers = response.headers;

	const char *header_connection = response_headers.Remove("connection");
	keep_alive = header_connection == nullptr ||
		!http_list_contains_i(header_connection, "close");

	if (http_status_is_empty(response.status) &&
	    /* "100 Continue" requires special handling here, because the
	       final response following it may contain a body */
	    response.status != HTTP_STATUS_CONTINUE)
		response.no_body = true;

	if (response.no_body || response.status == HTTP_STATUS_CONTINUE) {
		response.state = Response::State::END;
		return;
	}

	const char *transfer_encoding =
		response_headers.Remove("transfer-encoding");
	const char *content_length_string =
		response_headers.Remove("content-length");

	/* remove the other hop-by-hop response headers */
	response_headers.Remove("proxy-authenticate");

	const bool upgrade =
		transfer_encoding == nullptr && content_length_string == nullptr &&
		http_is_upgrade(response.status, response_headers);
	if (upgrade) {
		keep_alive = false;
	}

	off_t content_length;
	bool chunked;
	if (transfer_encoding == nullptr ||
	    strcasecmp(transfer_encoding, "chunked") != 0) {
		/* not chunked */

		if (content_length_string == nullptr) [[unlikely]] {
			if (keep_alive) {
				stopwatch.RecordEvent("malformed");

				throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
						      "no Content-Length response header");
			}
			content_length = (off_t)-1;
		} else {
			char *endptr;
			content_length = (off_t)strtoull(content_length_string,
							 &endptr, 10);
			if (endptr == content_length_string || *endptr != 0 ||
			    content_length < 0) [[unlikely]] {
				stopwatch.RecordEvent("malformed");

				throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
						      "invalid Content-Length header in response");
			}

			if (content_length == 0) {
				response.state = Response::State::END;
				return;
			}
		}

		chunked = false;
	} else {
		/* chunked */

		content_length = (off_t)-1;
		chunked = true;
	}

	response.body = response_body_reader.Init(event_loop,
						  content_length,
						  chunked);

	response.state = Response::State::BODY;
	if (!socket.IsReleased())
		socket.SetDirect(CheckDirect());
}

inline void
HttpClient::HandleLine(StringView line)
{
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS);

	if (response.state == Response::State::STATUS)
		ParseStatusLine(line);
	else if (!line.empty()) {
		if (!header_parse_line(caller_pool, response.headers, line))
			throw HttpClientError(HttpClientErrorCode::GARBAGE,
					      "malformed HTTP header line");
	} else
		HeadersFinished();
}

void
HttpClient::ResponseFinished() noexcept
{
	assert(response.state == Response::State::END);

	stopwatch.RecordEvent("end");

	if (!socket.IsEmpty()) {
		LogConcat(2, peer_name, "excess data after HTTP response");
		keep_alive = false;
	}

	if (!HasInput() && IsConnected())
		ReleaseSocket(false, keep_alive);

	Destroy();
}

inline BufferedResult
HttpClient::ParseHeaders(const StringView b)
{
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS);
	assert(!b.IsNull());
	assert(!b.empty());

	/* parse line by line */
	StringView remaining = b;
	while (true) {
		auto s = remaining.Split('\n');
		if (s.second == nullptr)
			break;

		StringView line = s.first;
		remaining = s.second;

		/* strip the line */
		line.StripRight();

		/* handle this line */
		HandleLine(line);

		if (response.state != Response::State::HEADERS) {
			/* header parsing is finished */
			socket.DisposeConsumed(remaining.data - b.data);
			return BufferedResult::AGAIN_EXPECT;
		}
	}

	/* remove the parsed part of the buffer */
	socket.DisposeConsumed(remaining.data - b.data);
	return BufferedResult::MORE;
}

void
HttpClient::ResponseBodyEOF() noexcept
{
	assert(response.state == Response::State::BODY);
	assert(response_body_reader.IsEOF());

	response.state = Response::State::END;

	auto *handler = response_body_reader.PrepareEof();

	ResponseFinished();

	if (handler != nullptr)
		handler->OnEof();
}

inline BufferedResult
HttpClient::FeedBody(std::span<const std::byte> b)
{
	assert(response.state == Response::State::BODY);

	std::size_t nbytes;

	{
		const DestructObserver destructed(*this);
		nbytes = response_body_reader.FeedBody(b.data(), b.size());

		if (!destructed && IsConnected())
			/* if BufferedSocket is currently flushing the
			   input buffer to start the "direct"
			   (=splice) transfer, and our response body
			   handler has just cleared its "direct" flag,
			   we need to keep BufferedSocket from doing
			   the "direct" transfer */
			socket.SetDirect(CheckDirect());

		if (nbytes == 0)
			return destructed
				? BufferedResult::CLOSED
				: BufferedResult::BLOCKING;
	}

	socket.DisposeConsumed(nbytes);

	if (IsConnected() && response_body_reader.IsSocketDone(socket))
		/* we don't need the socket anymore, we've got everything we
		   need in the input buffer */
		ReleaseSocket(true, keep_alive);

	if (response_body_reader.IsEOF()) {
		ResponseBodyEOF();
		return BufferedResult::CLOSED;
	}

	if (nbytes < b.size())
		return BufferedResult::OK;

	if (response_body_reader.RequireMore())
		return BufferedResult::MORE;

	return BufferedResult::OK;
}

BufferedResult
HttpClient::FeedHeaders(std::span<const std::byte> b)
{
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS);

	const BufferedResult result = ParseHeaders(StringView(b));
	if (result != BufferedResult::AGAIN_EXPECT)
		return result;

	/* the headers are finished, we can now report the response to
	   the handler */
	assert(response.state == Response::State::BODY ||
	       response.state == Response::State::END);

	if (response.status == HTTP_STATUS_CONTINUE) {
		assert(response.state == Response::State::END);

		if (!request.pending_body || !HasInput()) {
#ifndef NDEBUG
			/* assertion workaround */
			response.state = Response::State::STATUS;
#endif
			throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
					      "unexpected status 100");
		}

		if (!IsConnected()) {
#ifndef NDEBUG
			/* assertion workaround */
			response.state = HttpClient::Response::State::STATUS;
#endif
			throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
					      "Peer closed the socket prematurely after status 100");
		}

		/* reset state, we're now expecting the real response */
		response.state = Response::State::STATUS;

		request.pending_body->Resume();
		request.pending_body.reset();

		DeferWrite();

		/* try again */
		return BufferedResult::AGAIN_EXPECT;
	} else if (request.pending_body) {
		/* the server begins sending a response - he's not interested
		   in the request body, discard it now */
		request.pending_body->Discard();
		request.pending_body.reset();
	}

	if ((response.state == Response::State::END ||
	     response_body_reader.IsSocketDone(socket)) &&
	    IsConnected())
		/* we don't need the socket anymore, we've got everything we
		   need in the input buffer */
		ReleaseSocket(true, keep_alive);

	const DestructObserver destructed(*this);

	if (!response.body && !response.no_body)
		response.body = istream_null_new(caller_pool);

	if (response.state == Response::State::END) {
		auto &handler = request.handler;
		const auto status = response.status;
		auto headers = std::move(response.headers);
		auto body = std::move(response.body);

		ResponseFinished();
		handler.InvokeResponse(status, std::move(headers),
				       std::move(body));
		return BufferedResult::CLOSED;
	}

	response.in_handler = true;
	request.handler.InvokeResponse(response.status,
				       std::move(response.headers),
				       std::move(response.body));
	if (destructed)
		return BufferedResult::CLOSED;

	response.in_handler = false;

	if (response_body_reader.IsEOF()) {
		ResponseBodyEOF();
		return BufferedResult::CLOSED;
	}

	/* now do the response body */
	return response_body_reader.RequireMore()
		? BufferedResult::AGAIN_EXPECT
		: BufferedResult::AGAIN_OPTIONAL;
}

inline DirectResult
HttpClient::TryResponseDirect(SocketDescriptor fd, FdType fd_type)
{
	assert(IsConnected());
	assert(response.state == Response::State::BODY);
	assert(CheckDirect());

	switch (response_body_reader.TryDirect(fd, fd_type)) {
	case IstreamDirectResult::BLOCKING:
		/* the destination fd blocks */
		return DirectResult::BLOCKING;

	case IstreamDirectResult::CLOSED:
		/* the stream (and the whole connection) has been closed
		   during the direct() callback */
		return DirectResult::CLOSED;

	case IstreamDirectResult::ERRNO:
		if (errno == EAGAIN)
			/* the source fd (= ours) blocks */
			return DirectResult::EMPTY;

		return DirectResult::ERRNO;

	case IstreamDirectResult::END:
		if (HasInput())
			CloseInput();

		response_body_reader.SocketEOF(0);
		Destroy();
		return DirectResult::CLOSED;

	case IstreamDirectResult::OK:
		if (response_body_reader.IsEOF()) {
			ResponseBodyEOF();
			return DirectResult::CLOSED;
		}

		return DirectResult::OK;
	}

	gcc_unreachable();
}

/*
 * socket_wrapper handler
 *
 */

BufferedResult
HttpClient::OnBufferedData()
{
	switch (response.state) {
	case Response::State::STATUS:
	case Response::State::HEADERS:
		try {
			return FeedHeaders(socket.ReadBuffer());
		} catch (...) {
			AbortResponseHeaders(std::current_exception());
			return BufferedResult::CLOSED;
		}

	case Response::State::BODY:
		if (IsConnected() && response_body_reader.IsSocketDone(socket))
			/* we don't need the socket anymore, we've got everything
			   we need in the input buffer */
			ReleaseSocket(true, keep_alive);

		return FeedBody(socket.ReadBuffer());

	case Response::State::END:
		break;
	}

	assert(false);
	gcc_unreachable();
}

DirectResult
HttpClient::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
{
	return TryResponseDirect(fd, fd_type);

}

bool
HttpClient::OnBufferedHangup() noexcept
{
	stopwatch.RecordEvent("hup");

	if (HasInput())
		CloseInput();

	return true;
}

bool
HttpClient::OnBufferedClosed() noexcept
{
	stopwatch.RecordEvent("end");

	/* close the socket, but don't release it just yet; data may be
	   still in flight in a SocketFilter (e.g. SSL/TLS); we'll do that
	   in OnBufferedRemaining() which gets called after the
	   SocketFilter has completed */
	socket.Close();

	return true;
}

bool
HttpClient::OnBufferedRemaining(std::size_t remaining) noexcept
{
	if (!socket.IsReleased())
		/* by now, the SocketFilter has processed all incoming data,
		   and is available in the buffer; we can release the socket
		   lease, but keep the (decrypted) input buffer */
		/* note: the socket can't be reused, because it was closed by
		   the peer; this method gets called only after
		   OnBufferedClosed() */
		ReleaseSocket(true, false);

	if (remaining == 0 && response.state == Response::State::STATUS) {
		AbortResponseHeaders(HttpClientErrorCode::REFUSED,
				     "Server closed the socket without sending any response data");
		return false;
	}

	if (response.state < Response::State::BODY)
		/* this information comes too early, we can't use it */
		return true;

	if (response_body_reader.SocketEOF(remaining)) {
		/* there's data left in the buffer: continue serving the
		   buffer */
		return true;
	} else {
		/* finished: close the HTTP client */
		Destroy();
		return false;
	}
}

bool
HttpClient::OnBufferedWrite()
{
	request.got_data = false;

	switch (TryWriteBuckets()) {
	case HttpClient::BucketResult::MORE:
		break;

	case HttpClient::BucketResult::BLOCKING:
		return true;

	case HttpClient::BucketResult::DEPLETED:
		assert(!HasInput());
		socket.UnscheduleWrite();
		return true;

	case HttpClient::BucketResult::DESTROYED:
		return false;
	}

	const DestructObserver destructed(*this);

	input.Read();

	const bool result = !destructed && IsConnected();
	if (result && HasInput()) {
		if (request.got_data)
			ScheduleWrite();
		else
			socket.UnscheduleWrite();
	}

	return result;
}

enum write_result
HttpClient::OnBufferedBroken() noexcept
{
	/* the server has closed the connection, probably because he's not
	   interested in our request body - that's ok; now we wait for his
	   response */

	keep_alive = false;

	if (HasInput())
		CloseInput();

	socket.ScheduleReadNoTimeout(true);

	return WRITE_BROKEN;
}

void
HttpClient::OnBufferedError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("recv_error");
	AbortResponse(NestException(ep,
				    HttpClientError(HttpClientErrorCode::IO,
						    "HTTP client socket error")));
}

/*
 * istream handler for the request
 *
 */

std::size_t
HttpClient::OnData(const void *data, std::size_t length) noexcept
{
	assert(IsConnected());

	request.got_data = true;

	ssize_t nbytes = socket.Write({(const std::byte *)data, length});
	if (nbytes >= 0) [[likely]] {
		ScheduleWrite();
		return (std::size_t)nbytes;
	}

	if (nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED ||
	    nbytes == WRITE_BROKEN) [[likely]]
		return 0;

	int _errno = errno;

	stopwatch.RecordEvent("send_error");

	AbortResponse(NestException(std::make_exception_ptr(MakeErrno(_errno,
								      "Write error")),
				    HttpClientError(HttpClientErrorCode::IO,
						    "write error")));
	return 0;
}

IstreamDirectResult
HttpClient::OnDirect(FdType type, FileDescriptor fd, off_t offset,
		     std::size_t max_length) noexcept
{
	assert(IsConnected());

	request.got_data = true;

	ssize_t nbytes = socket.WriteFrom(fd, type, ToOffsetPointer(offset),
					  max_length);
	if (nbytes > 0) [[likely]] {
		input.ConsumeDirect(nbytes);
		ScheduleWrite();
		return IstreamDirectResult::OK;
	} else if (nbytes == WRITE_BLOCKING)
		return IstreamDirectResult::BLOCKING;
	else if (nbytes == WRITE_DESTROYED || nbytes == WRITE_BROKEN)
		return IstreamDirectResult::CLOSED;
	else if (nbytes == WRITE_SOURCE_EOF)
		return IstreamDirectResult::END;
	else {
		if (errno == EAGAIN) [[likely]] {
			request.got_data = false;
			socket.UnscheduleWrite();
		}

		return IstreamDirectResult::ERRNO;
	}
}

void
HttpClient::OnEof() noexcept
{
	stopwatch.RecordEvent("request_end");

	assert(HasInput());
	ClearInput();

	socket.UnscheduleWrite();
	socket.ScheduleReadNoTimeout(response.state == Response::State::BODY &&
				     response_body_reader.RequireMore());
}

void
HttpClient::OnError(std::exception_ptr ep) noexcept
{
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS ||
	       response.state == Response::State::BODY ||
	       response.state == Response::State::END);

	stopwatch.RecordEvent("request_error");

	assert(HasInput());
	ClearInput();

	switch (response.state) {
	case Response::State::STATUS:
	case Response::State::HEADERS:
		AbortResponseHeaders(ep);
		break;

	case Response::State::BODY:
		AbortResponseBody(ep);
		break;

	case Response::State::END:
		break;
	}
}

/*
 * async operation
 *
 */

inline void
HttpClient::Cancel() noexcept
{
	stopwatch.RecordEvent("cancel");

	/* Cancellable::Cancel() can only be used before the response was
	   delivered to our callback */
	assert(response.state == Response::State::STATUS ||
	       response.state == Response::State::HEADERS);

	Destroy();
}

/*
 * constructor
 *
 */

inline
HttpClient::HttpClient(struct pool &_pool, struct pool &_caller_pool,
		       StopwatchPtr &&_stopwatch,
		       FilteredSocket &_socket, Lease &lease,
		       const char *_peer_name,
		       http_method_t method, const char *uri,
		       const StringMap &headers,
		       GrowingBuffer &&headers2,
		       UnusedIstreamPtr body, bool expect_100,
		       HttpResponseHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
	:PoolLeakDetector(_pool),
	 pool(_pool), caller_pool(_caller_pool),
	 peer_name(_peer_name),
	 stopwatch(std::move(_stopwatch)),
	 event_loop(_socket.GetEventLoop()),
	 socket(_socket, lease,
		Event::Duration(-1), http_client_timeout,
		*this),
	 request(handler),
	 response_body_reader(pool)
{
	response.state = HttpClient::Response::State::STATUS;
	response.no_body = http_method_is_empty(method);

	cancel_ptr = *this;

	/* request line */

	const AllocatorPtr alloc(GetPool());
	const char *p = alloc.Concat(http_method_to_string(method), ' ', uri,
				     " HTTP/1.1\r\n");
	auto request_line_stream = istream_string_new(GetPool(), p);

	/* headers */

	const bool upgrade = body && http_is_upgrade(headers);
	if (upgrade) {
		/* forward hop-by-hop headers requesting the protocol
		   upgrade */
		header_write(headers2, "connection", "upgrade");

		const char *value = headers.Get("upgrade");
		if (value != nullptr)
			header_write(headers2, "upgrade", value);
	} else if (body) {
		off_t content_length = body.GetAvailable(false);
		if (content_length == (off_t)-1) {
			header_write(headers2, "transfer-encoding", "chunked");

			/* optimized code path: if an istream_dechunked shall get
			   chunked via istream_chunk, let's just skip both to
			   reduce the amount of work and I/O we have to do */
			if (!istream_dechunk_check_verbatim(body))
				body = istream_chunked_new(GetPool(), std::move(body));
		} else {
			snprintf(request.content_length_buffer,
				 sizeof(request.content_length_buffer),
				 "%lu", (unsigned long)content_length);
			header_write(headers2, "content-length",
				     request.content_length_buffer);
		}

		off_t available = expect_100 ? body.GetAvailable(true) : 0;
		if (available < 0 || available >= EXPECT_100_THRESHOLD) {
			/* large request body: ask the server for confirmation
			   that he's really interested */
			header_write(headers2, "expect", "100-continue");

			auto optional = istream_optional_new(GetPool(), std::move(body));
			body = std::move(optional.first);
			request.pending_body = std::move(optional.second);
		} else {
			/* short request body: send it immediately */
		}
	}

	headers_copy_most(headers, headers2);
	headers2.Write("\r\n", 2);

	auto header_stream = istream_gb_new(GetPool(), std::move(headers2));

	/* request istream */

	SetInput(NewConcatIstream(GetPool(),
				  std::move(request_line_stream),
				  std::move(header_stream),
				  std::move(body)));
	input.SetDirect(istream_direct_mask_to(socket.GetType()));

	socket.ScheduleReadNoTimeout(true);
	DeferWrite();
}

void
http_client_request(struct pool &caller_pool,
		    StopwatchPtr stopwatch,
		    FilteredSocket &socket, Lease &lease,
		    const char *peer_name,
		    http_method_t method, const char *uri,
		    const StringMap &headers,
		    GrowingBuffer &&more_headers,
		    UnusedIstreamPtr body, bool expect_100,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept
{
	assert(http_method_is_valid(method));

	if (!uri_path_verify_quick(uri)) {
		lease.ReleaseLease(true);
		body.Clear();

		handler.InvokeError(std::make_exception_ptr(HttpClientError(HttpClientErrorCode::UNSPECIFIED,
									    StringFormat<256>("malformed request URI '%s'", uri))));
		return;
	}

	NewFromPool<HttpClient>(caller_pool, caller_pool, caller_pool,
				std::move(stopwatch),
				socket,
				lease,
				peer_name,
				method, uri,
				headers, std::move(more_headers),
				std::move(body), expect_100,
				handler, cancel_ptr);
}
