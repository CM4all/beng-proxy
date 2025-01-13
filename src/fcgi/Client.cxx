// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Client.hxx"
#include "Error.hxx"
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
#include "event/net/BufferedSocket.hxx"
#include "net/SocketError.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/TimeoutError.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "util/DestructObserver.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "lease.hxx"

#include <fmt/format.h>

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
	  DestructAnchor
{
	BufferedSocket socket;

	LeasePtr lease_ref;

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

	std::size_t content_length = 0, skip_length = 0;

	/**
	 * How much STDERR content of the unconsumed input has already
	 * been handled?  We need to keep track of this because
	 * _FillBucketList() may be called multiple times without
	 * _ConsumeBucketList(), and only the first call shall handle
	 * STDERR content.
	 */
	std::size_t skip_stderr = 0;

public:
	FcgiClient(struct pool &_pool, EventLoop &event_loop,
		   StopwatchPtr &&_stopwatch,
		   SocketDescriptor fd, FdType fd_type, Lease &lease,
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
	 * Release the socket held by this object.
	 */
	void ReleaseSocket(PutAction action) noexcept {
		socket.Abandon();
		lease_ref.Release(action);
	}

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
	bool SubmitResponse(const DestructObserver &destructed) noexcept;

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
	bool HandleEnd() noexcept;

	/**
	 * A packet header was received.
	 *
	 * @return false if the client has been destroyed
	 */
	bool HandleHeader(const FcgiRecordHeader &header) noexcept;

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
};

static constexpr auto fcgi_client_timeout = std::chrono::minutes(2);

inline FcgiClient::~FcgiClient() noexcept
{
	if (socket.IsConnected())
		ReleaseSocket(PutAction::DESTROY);
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

FcgiClient::BufferAnalysis
FcgiClient::AnalyseBuffer(const std::span<const std::byte> buffer) const noexcept
{
	const auto data0 = (const std::byte *)buffer.data();
	const std::byte *data = data0, *const end = data0 + buffer.size();

	BufferAnalysis result;

	if (content_length > 0 && !response.stderr)
		result.total_stdout += content_length;

	/* skip the rest of the current packet */
	data += content_length + skip_length;

	while (true) {
		const FcgiRecordHeader &header =
			*(const FcgiRecordHeader *)data;
		data = (const std::byte *)(&header + 1);
		if (data > end)
			/* reached the end of the given buffer */
			break;

		const std::size_t new_content_length = header.content_length;

		data += new_content_length;
		data += header.padding_length;

		if (header.request_id == id) {
			if (header.type == FcgiRecordType::END_REQUEST) {
				/* found the END packet: stop here */
				result.end_request_offset = data - data0;
				break;
			} else if (header.type == FcgiRecordType::STDOUT) {
				result.total_stdout += new_content_length;
			}
		}
	}

	return result;
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
FcgiClient::SubmitResponse(const DestructObserver &destructed) noexcept
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
		skip_length += content_length;
		content_length = 0;
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

inline bool
FcgiClient::HandleEnd() noexcept
{
	assert(!response.end_request);
	response.end_request = true;

	skip_length += content_length;
	content_length = 0;

	stopwatch.RecordEvent("end");

	if (response.receiving_headers) {
		AbortResponseHeaders(std::make_exception_ptr(FcgiClientError(FcgiClientErrorCode::GARBAGE,
									     "premature end of headers "
									     "from FastCGI application")));
		return false;
	} else if (!response.no_body && response.available > 0) {
		AbortResponseBody(std::make_exception_ptr(FcgiClientError(FcgiClientErrorCode::GARBAGE,
									  "premature end of body "
									  "from FastCGI application")));
		return false;
	}

	response.available = 0;

	if (skip_length == 0) {
		HandleEndComplete();
		return false;
	}

	return true;
}

inline bool
FcgiClient::HandleHeader(const FcgiRecordHeader &header) noexcept
{
	content_length = header.content_length;
	skip_length = header.padding_length;

	if (header.request_id != id) {
		/* wrong request id; discard this packet */
		skip_length += content_length;
		content_length = 0;
		return true;
	}

	switch (header.type) {
	case FcgiRecordType::STDOUT:
		response.stderr = false;

		if (!response.receiving_headers && response.no_body) {
			/* ignore all payloads until END_REQUEST */
			skip_length += content_length;
			content_length = 0;
		}

		return true;

	case FcgiRecordType::STDERR:
		response.stderr = true;
		return true;

	case FcgiRecordType::END_REQUEST:
		return HandleEnd();

	default:
		skip_length += content_length;
		content_length = 0;
		return true;
	}
}

inline BufferedResult
FcgiClient::ConsumeInput(std::span<const std::byte> src) noexcept
{
	assert(!src.empty());

	const DestructObserver destructed(*this);

	while (true) {
		if (content_length > 0) {
			const bool at_headers = response.receiving_headers;

			auto payload = src;
			if (payload.size() > content_length)
				payload = payload.first(content_length);

			if (response.WasResponseSubmitted() &&
			    !response.stderr &&
			    response.available >= 0 &&
			    std::cmp_greater(payload.size(), response.available)) {
				/* the DATA packet was larger than the Content-Length
				   declaration - fail */
				AbortResponseBody(std::make_exception_ptr(FcgiClientError(FcgiClientErrorCode::GARBAGE,
											  "excess data at end of body "
											  "from FastCGI application")));
				return BufferedResult::DESTROYED;
			}

			std::size_t nbytes = Feed(payload);
			if (nbytes == 0) {
				if (destructed)
					return BufferedResult::DESTROYED;

				if (at_headers) {
					/* incomplete header line received, want more
					   data */
					assert(response.receiving_headers);
					break;
				}

				/* the response body handler blocks, wait for it to
				   become ready */
				return BufferedResult::OK;
			}

			src = src.subspan(nbytes);
			content_length -= nbytes;
			socket.DisposeConsumed(nbytes);

			if (at_headers && !response.receiving_headers) {
				/* the read_state has been switched from HEADERS to
				   BODY: we have to deliver the response now */

				return SubmitResponse(destructed)
					/* continue parsing the response body from the
					   buffer */
					? BufferedResult::AGAIN
					: BufferedResult::DESTROYED;
			}

			if (content_length > 0) {
				if (src.empty() || response.receiving_headers)
					/* all input was consumed, want more */
					break;

				/* some was consumed, try again later */
				return BufferedResult::OK;
			}

			continue;
		}

		if (skip_length > 0) {
			std::size_t nbytes = std::min(src.size(), skip_length);

			src = src.subspan(nbytes);
			skip_length -= nbytes;
			socket.DisposeConsumed(nbytes);

			if (skip_length > 0)
				return BufferedResult::MORE;

			if (response.end_request) {
				HandleEndComplete();
				return BufferedResult::DESTROYED;
			}

			continue;
		}

		const FcgiRecordHeader *header =
			(const FcgiRecordHeader *)src.data();
		if (src.size() < sizeof(*header))
			break;

		src = src.subspan(sizeof(*header));

		if (!HandleHeader(*header))
			return BufferedResult::DESTROYED;

		socket.DisposeConsumed(sizeof(*header));
	}

	return BufferedResult::MORE;
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

	const auto buffer = socket.ReadBuffer();
	if (buffer.size() > content_length) {
		const auto analysis = AnalyseBuffer(buffer);
		if (analysis.end_request_offset > 0 || partial)
			return analysis.total_stdout;
	}

	return partial && !response.stderr ? (off_t)content_length : -1;
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

void
FcgiClient::_FillBucketList(IstreamBucketList &list)
{
	assert(response.WasResponseSubmitted());

	if (response.available == 0 && !socket.IsConnected())
		return;

	auto b = socket.ReadBuffer();
	auto data = b.data();
	const auto end = data + b.size();

	off_t available = response.available;
	std::size_t current_content_length = content_length;
	std::size_t current_skip_length = skip_length;
	std::size_t current_skip_stderr = skip_stderr;
	std::size_t total_size = 0;
	bool current_stderr = response.stderr;

	bool found_end_request = response.end_request;

	while (true) {
		if (current_content_length > 0 && current_stderr) {
			const std::size_t remaining = end - data;
			const std::size_t size = std::min(remaining, current_content_length);

			if (size > current_skip_stderr) {
				std::span<const std::byte> src{data, size};
				src = src.subspan(current_skip_stderr);
				current_skip_stderr = 0;
				HandleStderrPayload(src);
				skip_stderr += src.size();
			} else {
				current_skip_stderr -= size;
			}

			data += size;
			current_content_length -= size;

			if (current_content_length > 0)
				break;
		}

		if (current_content_length > 0) {
			assert(!current_stderr);

			if (available >= 0 &&
			    std::cmp_greater(current_content_length, available)) {
				/* the DATA packet was larger than the Content-Length
				   declaration - fail */

				Destroy();

				throw FcgiClientError(FcgiClientErrorCode::GARBAGE,
						      "excess data at end of body "
						      "from FastCGI application");
			}

			const std::size_t remaining = end - data;
			if (remaining == 0)
				break;

			std::size_t size = std::min(remaining, current_content_length);
			if (available > 0) {
				if (std::cmp_greater(size, available))
					size = available;
				available -= size;
			}

			list.Push({data, size});
			data += size;
			current_content_length -= size;
			total_size += size;

			if (current_content_length > 0)
				break;
		}

		if (current_skip_length > 0) {
			const std::size_t remaining = end - data;
			std::size_t size = std::min(remaining, current_skip_length);
			data += size;
			current_skip_length -= size;

			if (current_skip_length > 0)
				break;
		}

		if (found_end_request) {
			if (socket.IsConnected())
				ReleaseSocket(data == end ? PutAction::REUSE : PutAction::DESTROY);
			break;
		}

		const auto &header = *(const FcgiRecordHeader *)data;
		const std::size_t remaining = end - data;
		if (remaining < sizeof(header))
			break;

		current_content_length = header.content_length;
		current_skip_length = header.padding_length;

		if (header.request_id != id) {
			/* ignore packets from other requests */
			current_skip_length += std::exchange(current_content_length, 0);
		} else if (header.type == FcgiRecordType::END_REQUEST) {
			/* "excess data" has already been checked */
			assert(response.available < 0 ||
			       static_cast<std::size_t>(response.available) >= total_size);

			if (available > 0) {
				Destroy();
				throw FcgiClientError(FcgiClientErrorCode::GARBAGE,
						      "premature end of body "
						      "from FastCGI application");
			} else if (response.available < 0) {
				/* now we know how much data remains */
				response.available = total_size;
			}

			found_end_request = true;
			current_skip_length += std::exchange(current_content_length, 0);
		} else if (header.type == FcgiRecordType::STDOUT) {
			current_stderr = false;
		} else if (header.type == FcgiRecordType::STDERR) {
			current_stderr = true;
		} else {
			/* ignore unknown packets */
			current_skip_length += std::exchange(current_content_length, 0);
		}

		data += sizeof(header);
	}

	/* report EOF only after we have received the whole
	   END_REQUEST payload/padding */
	if (!found_end_request || current_skip_length > 0)
		list.SetMore();
}

Istream::ConsumeBucketResult
FcgiClient::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	assert(response.available != 0);
	assert(response.WasResponseSubmitted());

	std::size_t total = 0;

	while (true) {
		if (content_length > 0) {
			std::size_t consumed = content_length;

			if (response.stderr) {
				if (const std::size_t a = socket.GetAvailable();
				    a < consumed)
					consumed = a;

				assert(consumed <= skip_stderr);
				skip_stderr -= consumed;
			} else {
				if (nbytes < consumed)
					consumed = nbytes;

				nbytes -= consumed;
				total += consumed;

				if (response.available > 0)
					response.available -= consumed;
			}

			assert(socket.GetAvailable() >= consumed);

			socket.DisposeConsumed(consumed);
			content_length -= consumed;

			if (content_length > 0)
				break;
		}

		if (skip_length > 0) {
			const auto b = socket.ReadBuffer();
			if (b.empty())
				break;

			std::size_t consumed = std::min(b.size(), skip_length);
			socket.DisposeConsumed(consumed);
			skip_length -= consumed;

			if (skip_length > 0)
				break;
		}

		if (response.end_request) {
			/* socket has been released by
			   _FillBucketList() already */
			assert(!socket.IsConnected());

			return {Consumed(total), true};
		}

		const auto b = socket.ReadBuffer();
		const auto &header = *(const FcgiRecordHeader *)b.data();
		if (b.size() < sizeof(header))
			break;

		content_length = header.content_length;
		skip_length = header.padding_length;

		if (header.request_id != id) {
			/* ignore packets from other requests */
			skip_length += std::exchange(content_length, 0);
		} else if (header.type == FcgiRecordType::END_REQUEST && !socket.IsConnected()) {
			/* this condition must have been detected
			   already in _FillBucketList() */
			assert(response.available == 0);

			response.end_request = true;
			skip_length += std::exchange(content_length, 0);
		} else if (header.type == FcgiRecordType::STDOUT) {
			response.stderr = false;
		} else if (header.type == FcgiRecordType::STDERR) {
			response.stderr = true;
		} else {
			/* ignore unknown packets */
			skip_length += std::exchange(content_length, 0);
		}

		socket.DisposeConsumed(sizeof(header));
	}

	socket.AfterConsumed();

	assert(nbytes == 0);

	return {Consumed(total), false};
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

	if (socket.IsConnected()) {
		/* check if the END_REQUEST packet can be found in the
		   following data chunk */
		const auto analysis = AnalyseBuffer(r);
		if (analysis.end_request_offset > 0 &&
		    analysis.end_request_offset <= r.size())
			/* found it: we no longer need the socket, everything we
			   need is already in the given buffer */
			ReleaseSocket(analysis.end_request_offset == r.size()
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
	ReleaseSocket(PutAction::DESTROY);
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
	assert(socket.IsConnected());

	stopwatch.RecordEvent("cancel");

	Destroy();
}

/*
 * constructor
 *
 */

inline
FcgiClient::FcgiClient(struct pool &_pool, EventLoop &event_loop,
		       StopwatchPtr &&_stopwatch,
		       SocketDescriptor fd, FdType fd_type, Lease &lease,
		       UniqueFileDescriptor &&_stderr_fd,
		       uint16_t _id, HttpMethod method,
		       UnusedIstreamPtr &&request_istream,
		       HttpResponseHandler &_handler,
		       CancellablePointer &cancel_ptr) noexcept
	:Istream(_pool),
	 IstreamSink(std::move(request_istream)),
	 socket(event_loop),
	 lease_ref(lease),
	 stderr_fd(std::move(_stderr_fd)),
	 stopwatch(std::move(_stopwatch)),
	 handler(_handler),
	 id(_id),
	 response(http_method_is_empty(method))
{
	socket.Init(fd, fd_type, fcgi_client_timeout, *this);

	input.SetDirect(istream_direct_mask_to(fd_type));

	cancel_ptr = *this;
}

void
fcgi_client_request(struct pool *pool, EventLoop &event_loop,
		    StopwatchPtr stopwatch,
		    SocketDescriptor fd, FdType fd_type, Lease &lease,
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
	    https != nullptr && strcmp(https, "on") == 0)
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

	auto client = NewFromPool<FcgiClient>(*pool, *pool, event_loop,
					      std::move(stopwatch),
					      fd, fd_type, lease,
					      std::move(stderr_fd),
					      header.request_id, method,
					      std::move(request),
					      handler, cancel_ptr);
	client->Start();
}
