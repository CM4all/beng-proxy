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
	size_t FeedHeaders(const void *data, size_t length);

	/**
	 * Call FeedHeaders() in a loop, to parse as much as possible.
	 *
	 * Caller must hold pool reference.
	 */
	size_t FeedHeadersLoop(const char *data, size_t length);

	/**
	 * Caller must hold pool reference.
	 */
	size_t FeedHeadersCheck(const char *data, size_t length);

	size_t FeedBody(const char *data, size_t length);

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class Istream */
	void _SetDirect(FdTypeMask mask) noexcept override {
		Istream::_SetDirect(mask);
		input.SetDirect(mask);
	}

	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;

	/* virtual methods from class IstreamHandler */
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

inline bool
CGIClient::ReturnResponse()
{
	http_status_t status = parser.GetStatus();
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

inline size_t
CGIClient::FeedHeaders(const void *data, size_t length)
try {
	assert(!parser.AreHeadersFinished());

	auto w = buffer.Write();
	assert(!w.empty());

	if (length > w.size())
		length = w.size();

	memcpy(w.data(), data, length);
	buffer.Append(length);

	switch (parser.FeedHeaders(GetPool(), buffer)) {
	case Completion::DONE:
		/* the DONE status can only be triggered by new data that
		   was just received; therefore, the amount of data still in
		   the buffer (= response body) must be smaller */
		assert(buffer.GetAvailable() < length);

		if (!ReturnResponse())
			return 0;

		/* don't consider data still in the buffer (= response body)
		   as "consumed"; the caller will attempt to submit it to the
		   response body handler */
		return length - buffer.GetAvailable();

	case Completion::MORE:
		return length;

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

inline size_t
CGIClient::FeedHeadersLoop(const char *data, size_t length)
{
	assert(length > 0);
	assert(!parser.AreHeadersFinished());

	const DestructObserver destructed(*this);
	size_t consumed = 0;

	do {
		size_t nbytes = FeedHeaders(data + consumed, length - consumed);
		if (nbytes == 0)
			break;

		consumed += nbytes;
	} while (consumed < length && !parser.AreHeadersFinished());

	if (destructed)
		return 0;

	return consumed;
}

inline size_t
CGIClient::FeedHeadersCheck(const char *data, size_t length)
{
	size_t nbytes = FeedHeadersLoop(data, length);

	assert(nbytes == 0 || input.IsDefined());
	assert(nbytes == 0 ||
	       !parser.AreHeadersFinished() ||
	       !parser.IsEOF());

	return nbytes;
}

inline size_t
CGIClient::FeedBody(const char *data, size_t length)
{
	if (parser.IsTooMuch(length)) {
		stopwatch.RecordEvent("malformed");
		DestroyError(std::make_exception_ptr(CgiError("too much data from CGI script")));
		return 0;
	}

	had_output = true;

	size_t nbytes = InvokeData(data, length);
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

size_t
CGIClient::OnData(const void *data, size_t length) noexcept
{
	assert(input.IsDefined());

	had_input = true;

	if (!parser.AreHeadersFinished()) {
		size_t nbytes = FeedHeadersCheck((const char *)data, length);

		if (nbytes > 0 && nbytes < length &&
		    parser.AreHeadersFinished()) {
			/* the headers are finished; now begin sending the
			   response body */
			const DestructObserver destructed(*this);
			size_t nbytes2 = FeedBody((const char *)data + nbytes,
						  length - nbytes);
			if (nbytes2 > 0)
				/* more data was consumed */
				nbytes += nbytes2;
			else if (destructed)
				/* the connection was closed, must return 0 */
				nbytes = 0;
		}

		return nbytes;
	} else {
		return FeedBody((const char *)data, length);
	}
}

ssize_t
CGIClient::OnDirect(FdType type, int fd, size_t max_length) noexcept
{
	assert(parser.AreHeadersFinished());

	had_input = true;
	had_output = true;

	if (parser.KnownLength() &&
	    (off_t)max_length > parser.GetAvailable())
		max_length = (size_t)parser.GetAvailable();

	ssize_t nbytes = InvokeDirect(type, fd, max_length);
	if (nbytes > 0 && parser.BodyConsumed(nbytes)) {
		stopwatch.RecordEvent("end");
		DestroyEof();
		return ISTREAM_RESULT_CLOSED;
	}

	return nbytes;
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
