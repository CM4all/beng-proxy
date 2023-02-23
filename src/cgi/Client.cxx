// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Client.hxx"
#include "Error.hxx"
#include "Parser.hxx"
#include "pool/pool.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_null.hxx"
#include "stopwatch.hxx"
#include "http/ResponseHandler.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/DestructObserver.hxx"
#include "util/Exception.hxx"

#include <string.h>
#include <stdlib.h>

class CGIClient final : Istream, IstreamSink, Cancellable, DestructAnchor {
	const StopwatchPtr stopwatch;

	SliceFifoBuffer buffer;

	CGIParser parser;

	/**
	 * This flag is true while cgi_parse_headers() is calling
	 * HttpResponseHandler::InvokeResponse().  In this case,
	 * istream_read(cgi->input) is already up in the stack, and must
	 * not be called again.
	 */
	bool in_response_callback;

	bool had_input, had_output;

	HttpResponseHandler &handler;

public:
	CGIClient(struct pool &_pool, StopwatchPtr &&_stopwatch,
		  UnusedIstreamPtr _input,
		  HttpResponseHandler &_handler,
		  CancellablePointer &cancel_ptr);

	/**
	 * @return false if the connection has been closed
	 */
	bool ReturnResponse();

	/**
	 * Feed data into the input buffer and continue parsing response
	 * headers from it.  After this function returns, the response may
	 * have been delivered to the response handler, and the caller should
	 * post the rest of the specified buffer to the response body stream.
	 *
	 * Caller must hold pool reference.
	 *
	 * @return the number of bytes consumed from the specified buffer
	 * (moved to the input buffer), 0 if the object has been closed
	 */
	std::size_t FeedHeaders(std::span<const std::byte> src);

	/**
	 * Call FeedHeaders() in a loop, to parse as much as possible.
	 *
	 * Caller must hold pool reference.
	 */
	std::size_t FeedHeadersLoop(std::span<const std::byte> src);

	/**
	 * Caller must hold pool reference.
	 */
	std::size_t FeedHeadersCheck(std::span<const std::byte> src);

	std::size_t FeedBody(std::span<const std::byte> src);

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class Istream */
	void _SetDirect(FdTypeMask mask) noexcept override {
		Istream::_SetDirect(mask);
		input.SetDirect(mask);
	}

	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

inline bool
CGIClient::ReturnResponse()
{
	const auto status = parser.GetStatus();
	auto headers = std::move(parser).GetHeaders();

	if (http_status_is_empty(status)) {
		/* this response does not have a response body, as indicated
		   by the HTTP status code */

		stopwatch.RecordEvent("empty");

		auto &_handler = handler;
		Destroy();
		_handler.InvokeResponse(status, std::move(headers), UnusedIstreamPtr());
		return false;
	} else if (parser.IsEOF()) {
		/* the response body is empty */

		stopwatch.RecordEvent("empty");

		auto &_handler = handler;
		Destroy();
		_handler.InvokeResponse(status, std::move(headers),
					istream_null_new(GetPool()));
		return false;
	} else {
		stopwatch.RecordEvent("headers");

		const DestructObserver destructed(*this);

		in_response_callback = true;
		handler.InvokeResponse(status, std::move(headers),
				       UnusedIstreamPtr(this));
		if (destructed)
			return false;

		in_response_callback = false;
		return true;
	}
}

inline std::size_t
CGIClient::FeedHeaders(std::span<const std::byte> src)
try {
	assert(!parser.AreHeadersFinished());

	auto w = buffer.Write();
	assert(!w.empty());

	if (src.size() > w.size())
		src = src.first(w.size());

	std::copy(src.begin(), src.end(), w.data());
	buffer.Append(src.size());

	switch (parser.FeedHeaders(GetPool(), buffer)) {
	case Completion::DONE:
		/* the DONE status can only be triggered by new data that
		   was just received; therefore, the amount of data still in
		   the buffer (= response body) must be smaller */
		assert(buffer.GetAvailable() < src.size());

		if (!ReturnResponse())
			return 0;

		/* don't consider data still in the buffer (= response body)
		   as "consumed"; the caller will attempt to submit it to the
		   response body handler */
		return src.size() - buffer.GetAvailable();

	case Completion::MORE:
		return src.size();

	case Completion::CLOSED:
		/* unreachable */
		assert(false);
		return 0;
	}

	/* unreachable */
	assert(false);
	return 0;
} catch (...) {
	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::current_exception());
	return 0;
}

inline std::size_t
CGIClient::FeedHeadersLoop(const std::span<const std::byte> src)
{
	assert(!src.empty());
	assert(!parser.AreHeadersFinished());

	const DestructObserver destructed(*this);
	std::size_t consumed = 0;

	do {
		std::size_t nbytes = FeedHeaders(src.subspan(consumed));
		if (nbytes == 0)
			break;

		consumed += nbytes;
	} while (consumed < src.size() && !parser.AreHeadersFinished());

	if (destructed)
		return 0;

	return consumed;
}

inline std::size_t
CGIClient::FeedHeadersCheck(std::span<const std::byte> src)
{
	std::size_t nbytes = FeedHeadersLoop(src);

	assert(nbytes == 0 || input.IsDefined());
	assert(nbytes == 0 ||
	       !parser.AreHeadersFinished() ||
	       !parser.IsEOF());

	return nbytes;
}

inline std::size_t
CGIClient::FeedBody(std::span<const std::byte> src)
{
	if (parser.IsTooMuch(src.size())) {
		stopwatch.RecordEvent("malformed");
		DestroyError(std::make_exception_ptr(CgiError("too much data from CGI script")));
		return 0;
	}

	had_output = true;

	std::size_t nbytes = InvokeData(src);
	if (nbytes > 0 && parser.BodyConsumed(nbytes)) {
		stopwatch.RecordEvent("end");
		DestroyEof();
		return 0;
	}

	return nbytes;
}

/*
 * input handler
 *
 */

std::size_t
CGIClient::OnData(std::span<const std::byte> src) noexcept
{
	assert(input.IsDefined());

	had_input = true;

	if (!parser.AreHeadersFinished()) {
		std::size_t nbytes = FeedHeadersCheck(src);

		if (nbytes > 0 && nbytes < src.size() &&
		    parser.AreHeadersFinished()) {
			/* the headers are finished; now begin sending the
			   response body */
			const DestructObserver destructed(*this);
			std::size_t nbytes2 = FeedBody(src.subspan(nbytes));
			if (nbytes2 > 0)
				/* more data was consumed */
				nbytes += nbytes2;
			else if (destructed)
				/* the connection was closed, must return 0 */
				nbytes = 0;
		}

		return nbytes;
	} else {
		return FeedBody(src);
	}
}

IstreamDirectResult
CGIClient::OnDirect(FdType type, FileDescriptor fd, off_t offset,
		    std::size_t max_length) noexcept
{
	assert(parser.AreHeadersFinished());

	had_input = true;
	had_output = true;

	if (parser.KnownLength() &&
	    (off_t)max_length > parser.GetAvailable())
		max_length = (std::size_t)parser.GetAvailable();

	auto result = InvokeDirect(type, fd, offset, max_length);
	if (result == IstreamDirectResult::OK && parser.IsEOF()) {
		stopwatch.RecordEvent("end");
		DestroyEof();
		result = IstreamDirectResult::CLOSED;
	}

	return result;
}

void
CGIClient::OnEof() noexcept
{
	input.Clear();

	if (!parser.AreHeadersFinished()) {
		stopwatch.RecordEvent("malformed");

		assert(!HasHandler());

		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::make_exception_ptr(CgiError("premature end of headers from CGI script")));
	} else if (parser.DoesRequireMore()) {
		stopwatch.RecordEvent("malformed");

		DestroyError(std::make_exception_ptr(CgiError("premature end of response body from CGI script")));
	} else {
		stopwatch.RecordEvent("end");

		DestroyEof();
	}
}

void
CGIClient::OnError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("error");

	input.Clear();

	if (!parser.AreHeadersFinished()) {
		/* the response hasn't been sent yet: notify the response
		   handler */
		assert(!HasHandler());

		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(NestException(ep,
						   std::runtime_error("CGI request body failed")));
	} else {
		/* response has been sent: abort only the output stream */
		DestroyError(ep);
	}
}

/*
 * istream implementation
 *
 */

off_t
CGIClient::_GetAvailable(bool partial) noexcept
{
	if (parser.KnownLength())
		return parser.GetAvailable();

	if (!input.IsDefined())
		return 0;

	if (in_response_callback)
		/* this condition catches the case in cgi_parse_headers():
		   HttpResponseHandler::InvokeResponse() might
		   recursively call istream_read(input) */
		return (off_t)-1;

	return input.GetAvailable(partial);
}

void
CGIClient::_Read() noexcept
{
	if (input.IsDefined()) {
		/* this condition catches the case in cgi_parse_headers():
		   HttpResponseHandler::InvokeResponse() might
		   recursively call input.Read() */
		if (in_response_callback) {
			return;
		}

		const DestructObserver destructed(*this);

		had_output = false;
		do {
			had_input = false;
			input.Read();
		} while (!destructed && input.IsDefined() && had_input && !had_output);
	}
}

void
CGIClient::_ConsumeDirect(std::size_t nbytes) noexcept
{
	parser.BodyConsumed(nbytes);
}

/*
 * async operation
 *
 */

void
CGIClient::Cancel() noexcept
{
	assert(input.IsDefined());

	Destroy();
}


/*
 * constructor
 *
 */

inline
CGIClient::CGIClient(struct pool &_pool, StopwatchPtr &&_stopwatch,
		     UnusedIstreamPtr _input,
		     HttpResponseHandler &_handler,
		     CancellablePointer &cancel_ptr)
	:Istream(_pool), IstreamSink(std::move(_input)),
	 stopwatch(std::move(_stopwatch)),
	 buffer(fb_pool_get()),
	 handler(_handler)
{
	cancel_ptr = *this;

	input.Read();
}

void
cgi_client_new(struct pool &pool, StopwatchPtr stopwatch,
	       UnusedIstreamPtr input,
	       HttpResponseHandler &handler,
	       CancellablePointer &cancel_ptr)
{
	NewFromPool<CGIClient>(pool, pool, std::move(stopwatch),
			       std::move(input),
			       handler, cancel_ptr);
}
