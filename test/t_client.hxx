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

#include "http/ResponseHandler.hxx"
#include "lease.hxx"
#include "istream/istream.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/ApproveIstream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/FailIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/ZeroIstream.hxx"
#include "pool/pool.hxx"
#include "event/FineTimerEvent.hxx"
#include "PInstance.hxx"
#include "strmap.hxx"
#include "memory/fb_pool.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#ifdef USE_BUCKETS
#include "istream/Bucket.hxx"
#endif

#ifdef HAVE_EXPECT_100
#include "http/Client.hxx"
#endif

#include "http/Method.h"

#include <stdexcept>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#ifndef HAVE_CHUNKED_REQUEST_BODY
static constexpr size_t HEAD_SIZE = 16384;
#endif

static PoolPtr
NewMajorPool(struct pool &parent, const char *name) noexcept
{
	auto pool = pool_new_dummy(&parent, name);
	pool_set_major(pool);
	return pool;
}

template<class Connection>
struct Context final
	: PInstance, Cancellable, Lease, HttpResponseHandler, IstreamSink {

	PoolPtr parent_pool;

	PoolPtr pool;

	unsigned data_blocking = 0;

	off_t close_response_body_after = -1;

	/**
	 * Call EventLoop::Break() as soon as response headers (or a
	 * response error) is received?
	 */
	bool break_response = false;

	/**
	 * Call istream_read() on the response body from inside the
	 * response callback.
	 */
	bool read_response_body = false;

	/**
	 * Defer a call to Istream::Read().
	 */
	bool defer_read_response_body = false;

	bool close_response_body_early = false;
	bool close_response_body_late = false;
	bool close_response_body_data = false;
	bool response_body_byte = false;
	CancellablePointer cancel_ptr;
	Connection *connection = nullptr;
	bool released = false, reuse, aborted = false;
	http_status_t status = http_status_t(0);
	std::exception_ptr request_error;

	char *content_length = nullptr;
	off_t available = 0;

	DelayedIstreamControl *delayed = nullptr;

	off_t body_data = 0, consumed_body_data = 0;
	bool body_eof = false, body_abort = false, body_closed = false;

	DelayedIstreamControl *request_body = nullptr;
	bool aborted_request_body = false;
	bool close_request_body_early = false, close_request_body_eof = false;
	std::exception_ptr body_error;

#ifdef USE_BUCKETS
	bool use_buckets = false;
	bool more_buckets;
	bool read_after_buckets = false, close_after_buckets = false;
	size_t total_buckets;
	off_t available_after_bucket, available_after_bucket_partial;
#endif

	FineTimerEvent defer_event;
	bool deferred = false;

	Context() noexcept
		:parent_pool(NewMajorPool(root_pool, "parent")),
		 pool(pool_new_linear(parent_pool, "test", 16384)),
		 defer_event(event_loop, BIND_THIS_METHOD(OnDeferred)) {
	}

	~Context() noexcept {
		free(content_length);
		parent_pool.reset();
	}

	using IstreamSink::HasInput;
	using IstreamSink::CloseInput;

	bool WaitingForResponse() const noexcept {
		return status == http_status_t(0) && !request_error;
	}

	void WaitForResponse() noexcept {
		break_response = true;

		if (WaitingForResponse())
			event_loop.Dispatch();

		assert(!WaitingForResponse());

		break_response = false;
	}

	void WaitForFirstBodyByte() noexcept {
		assert(status != http_status_t(0));
		assert(!request_error);

		while (body_data == 0 && HasInput()) {
			assert(!body_eof);
			assert(body_error == nullptr);

			ReadBody();
			event_loop.LoopNonBlock();
		}
	}

	void WaitForEndOfBody() noexcept {
		while (HasInput()) {
			ReadBody();
			event_loop.LoopNonBlock();
		}
	}

	/**
	 * Give the client library another chance to release the
	 * socket/process.  This is a workaround for spurious unit test
	 * failures with the AJP client.
	 */
	void WaitReleased() noexcept {
		if (!released)
			event_loop.LoopNonBlock();
	}

#ifdef USE_BUCKETS
	void DoBuckets() noexcept {
		IstreamBucketList list;

		try {
			input.FillBucketList(list);
		} catch (...) {
			body_error = std::current_exception();
			return;
		}

		more_buckets = list.HasMore();
		total_buckets = list.GetTotalBufferSize();

		if (total_buckets > 0) {
			size_t buckets_consumed = input.ConsumeBucketList(total_buckets);
			assert(buckets_consumed == total_buckets);
			body_data += buckets_consumed;
		}

		available_after_bucket = input.GetAvailable(false);
		available_after_bucket_partial = input.GetAvailable(true);

		if (read_after_buckets)
			input.Read();

		if (close_after_buckets) {
			body_closed = true;
			CloseInput();
			close_response_body_late = false;
		}
	}
#endif

	void OnDeferred() noexcept {
		if (defer_read_response_body) {
			deferred = false;
			input.Read();
			return;
		}

#ifdef USE_BUCKETS
		if (use_buckets) {
			available = input.GetAvailable(false);
			DoBuckets();
		} else
#endif
			assert(false);
	}

	void ReadBody() noexcept {
		assert(HasInput());

#ifdef USE_BUCKETS
		if (use_buckets)
			DoBuckets();
		else
#endif
			input.Read();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	void ReleaseLease(bool _reuse) noexcept override {
		assert(connection != nullptr);

		delete connection;
		connection = nullptr;
		released = true;
		reuse = _reuse;
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

template<class Connection>
void
Context<Connection>::Cancel() noexcept
{
	assert(request_body != nullptr);
	assert(!aborted_request_body);

	request_body = nullptr;
	aborted_request_body = true;
}

/*
 * istream handler
 *
 */

template<class Connection>
std::size_t
Context<Connection>::OnData(std::span<const std::byte> src) noexcept
{
	body_data += src.size();

	if (close_response_body_after >= 0 &&
	    body_data >= close_response_body_after)
		close_response_body_data = true;

	if (close_response_body_data) {
		body_closed = true;
		CloseInput();
		return 0;
	}

	if (data_blocking) {
		--data_blocking;
		event_loop.Break();
		return 0;
	}

	if (deferred)
		return 0;

	consumed_body_data += src.size();
	return src.size();
}

template<class Connection>
void
Context<Connection>::OnEof() noexcept
{
	ClearInput();
	body_eof = true;

	if (close_request_body_eof && !aborted_request_body) {
		request_body->SetError(std::make_exception_ptr(std::runtime_error("close_request_body_eof")));
	}
}

template<class Connection>
void
Context<Connection>::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();
	body_abort = true;

	defer_event.Cancel();

	assert(!body_error);
	body_error = ep;
}

/*
 * http_response_handler
 *
 */

template<class Connection>
void
Context<Connection>::OnHttpResponse(http_status_t _status,
				    StringMap &&headers,
				    UnusedIstreamPtr _body) noexcept
{
	if (break_response)
		event_loop.Break();

	status = _status;
	const char *_content_length = headers.Get("content-length");
	if (_content_length != nullptr)
		content_length = strdup(_content_length);
	available = _body
		? _body.GetAvailable(false)
		: -2;

	if (close_request_body_early && !aborted_request_body) {
		request_body->SetError(std::make_exception_ptr(std::runtime_error("close_request_body_early")));
	}

	if (response_body_byte) {
		assert(_body);
		_body = istream_byte_new(*pool, std::move(_body));
	}

	if (close_response_body_early)
		_body.Clear();
	else if (_body)
		SetInput(std::move(_body));

#ifdef USE_BUCKETS
	if (use_buckets) {
		if (available >= 0)
			DoBuckets();
		else {
			/* try again later */
			defer_event.Schedule(std::chrono::milliseconds(10));
			deferred = true;
		}

		return;
	}
#endif

	if (read_response_body)
		ReadBody();

	if (defer_read_response_body) {
		defer_event.Schedule(Event::Duration{});
		deferred = true;
	}

	if (close_response_body_late) {
		body_closed = true;
		CloseInput();
	}

	if (delayed != nullptr) {
		std::runtime_error error("delayed_fail");
		delayed->Set(istream_fail_new(*pool, std::make_exception_ptr(error)));
	}

	pool.reset();

	fb_pool_compress();
}

template<class Connection>
void
Context<Connection>::OnHttpError(std::exception_ptr ep) noexcept
{
	if (break_response)
		event_loop.Break();

	assert(!request_error);
	request_error = ep;

	aborted = true;

	pool.reset();
}

/*
 * tests
 *
 */

template<class Connection>
static void
test_empty(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);
	pool_commit();

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

template<class Connection>
static void
test_body(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HTTP_STATUS_OK);
	assert(!c.request_error);
	assert(c.content_length == nullptr);
	assert(c.available == 6);

	c.WaitForFirstBodyByte();
	c.WaitReleased();

	assert(c.released);
	assert(c.body_eof);
	assert(c.body_data == 6);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

/**
 * Call istream_read() on the response body from inside the response
 * callback.
 */
template<class Connection>
static void
test_read_body(Context<Connection> &c) noexcept
{
	c.read_response_body = true;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available == 6);
	assert(c.body_eof);
	assert(c.body_data == 6);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

#ifdef ENABLE_HUGE_BODY

/**
 * A huge response body with declared Content-Length.
 */
template<class Connection>
static void
test_huge(Context<Connection> &c) noexcept
{
	c.read_response_body = true;
	c.close_response_body_data = true;
	c.connection = Connection::NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.available >= 65536);
	assert(!c.body_eof);
	assert(c.body_data > 0);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

#endif

template<class Connection>
static void
test_close_response_body_early(Context<Connection> &c) noexcept
{
	c.close_response_body_early = true;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available == 6);
	assert(!c.HasInput());
	assert(c.body_data == 0);
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_close_response_body_late(Context<Connection> &c) noexcept
{
	c.close_response_body_late = true;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available == 6);
	assert(!c.HasInput());
	assert(c.body_data == 0);
	assert(!c.body_eof);
	assert(c.body_abort || c.body_closed);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_close_response_body_data(Context<Connection> &c) noexcept
{
	c.close_response_body_data = true;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HTTP_STATUS_OK);
	assert(!c.request_error);
	assert(c.content_length == nullptr);
	assert(c.available == 6);

	c.WaitForFirstBodyByte();

	assert(c.released);
	assert(!c.HasInput());
	assert(c.body_data == 6);
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(c.body_closed);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_close_response_body_after(Context<Connection> &c) noexcept
{
	c.close_response_body_after = 16384;
	c.connection = Connection::NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HTTP_STATUS_OK);
	assert(!c.request_error);
	assert(c.content_length == nullptr);
	assert(c.available == 524288);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(!c.HasInput());
	assert(c.body_data >= 16384);
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(c.body_closed);
	assert(c.body_error == nullptr);
}

static UnusedIstreamPtr
wrap_fake_request_body([[maybe_unused]] struct pool *pool, UnusedIstreamPtr i)
{
#ifndef HAVE_CHUNKED_REQUEST_BODY
	if (i.GetAvailable(false) < 0)
		i = istream_head_new(*pool, std::move(i), HEAD_SIZE, true);
#endif
	return i;
}

template<class Connection>
static UnusedIstreamPtr
make_delayed_request_body(Context<Connection> &c) noexcept
{
	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	delayed.second.cancel_ptr = c;
	c.request_body = &delayed.second;
	return std::move(delayed.first);
}

template<class Connection>
static void
test_close_request_body_early(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	const std::runtime_error error("fail_request_body_early");
	c.request_body->SetError(std::make_exception_ptr(error));

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == 0);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(c.body_error == nullptr);
	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), error.what()) != nullptr);
}

template<class Connection>
static void
test_close_request_body_fail(Context<Connection> &c) noexcept
{
	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	auto request_body =
		NewConcatIstream(*c.pool,
				 istream_head_new(*c.pool, istream_zero_new(*c.pool),
						  4096, false),
				 std::move(delayed.first));

	c.delayed = &delayed.second;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, std::move(request_body)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == 200);
	assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
	assert(c.available == -1);
#else
	assert(c.available == HEAD_SIZE);
#endif
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(c.body_abort);

	if (c.body_error != nullptr && !c.request_error) {
		c.request_error = std::exchange(c.body_error, std::exception_ptr());
	}

	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), "delayed_fail") != nullptr);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_data_blocking(Context<Connection> &c) noexcept
{
	auto [request_body, approve_control] =
		NewApproveIstream(*c.pool, c.event_loop,
				  istream_head_new(*c.pool,
						   istream_zero_new(*c.pool),
						   65536, false));

	c.data_blocking = 5;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, std::move(request_body)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.request_error);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
	assert(c.available == -1);
#else
	assert(c.available == HEAD_SIZE);
#endif
	assert(c.HasInput());
	assert(!c.released);

	approve_control->Approve(16);

	while (c.data_blocking > 0) {
		assert(c.HasInput());

		c.ReadBody();
		c.event_loop.Dispatch();
	}

	approve_control.reset();

	assert(!c.released);
	assert(c.HasInput());
	assert(c.body_data > 0);
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);

	c.CloseInput();

	assert(c.released);
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);

	/* flush all remaining events */
	c.event_loop.Dispatch();
}

/**
 * This produces a closed socket while the HTTP client has data left
 * in the buffer.
 */
template<class Connection>
static void
test_data_blocking2(Context<Connection> &c) noexcept
{
	StringMap request_headers;
	request_headers.Add(*c.pool, "connection", "close");

	constexpr size_t body_size = 256;

	c.response_body_byte = true;
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", std::move(request_headers),
			      istream_head_new(*c.pool, istream_zero_new(*c.pool),
					       body_size, true),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HTTP_STATUS_OK);
	assert(!c.request_error);

	c.WaitForFirstBodyByte();

	/* the socket is released by now, but the body isn't finished
	   yet */
#ifndef NO_EARLY_RELEASE_SOCKET
	if (!c.released) {
		/* just in case we experienced a partial read and the socket
		   wasn't released yet: try again after some delay, to give
		   the server process another chance to send the final byte */
		usleep(1000);
		c.event_loop.LoopNonBlock();
	}

	assert(c.released);
#endif
	assert(c.content_length == nullptr);
	assert(c.available == body_size);
	assert(c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(c.consumed_body_data < (off_t)body_size);
	assert(c.body_error == nullptr);

	/* receive the rest of the response body from the buffer */
	c.WaitForEndOfBody();

	assert(c.released);
	assert(c.body_eof);
	assert(!c.body_abort);
	assert(c.consumed_body_data == body_size);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_body_fail(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);

	const std::runtime_error error("body_fail");

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_fail_new(*c.pool, std::make_exception_ptr(error))),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.aborted || c.body_abort);

	if (c.body_error != nullptr && !c.request_error) {
		c.request_error = std::exchange(c.body_error, std::exception_ptr());
	}

	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), error.what()) != nullptr);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_head(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_HEAD, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length != nullptr);
	assert(strcmp(c.content_length, "6") == 0);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

/**
 * Send a HEAD request.  The server sends a response body, and the
 * client library is supposed to discard it.
 */
template<class Connection>
static void
test_head_discard(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewFixed(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_HEAD, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_OK);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

/**
 * Same as test_head_discard(), but uses Connection::NewTiny)(*c.pool).
 */
template<class Connection>
static void
test_head_discard2(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewTiny(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_HEAD, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length != nullptr);
	[[maybe_unused]]
		unsigned long content_length = strtoul(c.content_length, nullptr, 10);
	assert(content_length == 5 || content_length == 256);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_ignored_body(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_zero_new(*c.pool)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY

/**
 * Close request body in the response handler (with response body).
 */
template<class Connection>
static void
test_close_ignored_request_body(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewNull(*c.pool, c.event_loop);
	c.close_request_body_early = true;
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

/**
 * Close request body in the response handler, method HEAD (no
 * response body).
 */
template<class Connection>
static void
test_head_close_ignored_request_body(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewNull(*c.pool, c.event_loop);
	c.close_request_body_early = true;
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_HEAD, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
template<class Connection>
static void
test_close_request_body_eor(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewDummy(*c.pool, c.event_loop);
	c.close_request_body_eof = true;
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
template<class Connection>
static void
test_close_request_body_eor2(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewFixed(*c.pool, c.event_loop);
	c.close_request_body_eof = true;
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      make_delayed_request_body(c),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

#endif

#ifdef HAVE_EXPECT_100

/**
 * Check if the HTTP client handles "100 Continue" received without
 * announcing the expectation.
 */
template<class Connection>
static void
test_bogus_100(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewTwice100(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr, false,
			      c, c.cancel_ptr);


	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.aborted);
	assert(c.request_error);

	const auto *e = FindNested<HttpClientError>(c.request_error);
	(void)e;
	assert(e != nullptr);
	assert(e->GetCode() == HttpClientErrorCode::UNSPECIFIED);

	assert(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
}

/**
 * Check if the HTTP client handles "100 Continue" received twice
 * well.
 */
template<class Connection>
static void
test_twice_100(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewTwice100(*c.pool, c.event_loop);
	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	delayed.second.cancel_ptr = nullptr;
	c.request_body = &delayed.second;
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      std::move(delayed.first),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.aborted);
	assert(c.request_error);

	const auto *e = FindNested<HttpClientError>(c.request_error);
	(void)e;
	assert(e != nullptr);
	assert(e->GetCode() == HttpClientErrorCode::UNSPECIFIED);

	assert(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
}

/**
 * The server sends "100 Continue" and closes the socket.
 */
template<class Connection>
static void
test_close_100(Context<Connection> &c) noexcept
{
	auto request_body = istream_delayed_new(*c.pool, c.event_loop);
	request_body.second.cancel_ptr = nullptr;

	c.connection = Connection::NewClose100(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_POST, "/foo", {},
			      std::move(request_body.first), true,
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.aborted);
	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), "closed the socket prematurely") != nullptr ||
	       /* the following two errors are not the primary error,
		  but sometimes occur depending on the timing: */
	       strstr(GetFullMessage(c.request_error).c_str(), "Connection reset by peer") != nullptr ||
	       strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
}

#endif

/**
 * Receive an empty response from the server while still sending the
 * request body.
 */
template<class Connection>
static void
test_no_body_while_sending(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_NO_CONTENT);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_hold(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool)),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(c.body_abort);
	assert(!c.request_error);
	assert(c.body_error != nullptr);
}

#ifdef ENABLE_PREMATURE_CLOSE_HEADERS

/**
 * The server closes the connection before it finishes sending the
 * response headers.
 */
template<class Connection>
static void
test_premature_close_headers(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewPrematureCloseHeaders(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == 0);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_abort);
	assert(c.request_error != nullptr);
	assert(!c.reuse);
}

#endif

#ifdef ENABLE_PREMATURE_CLOSE_BODY

/**
 * The server closes the connection before it finishes sending the
 * response body.
 */
template<class Connection>
static void
test_premature_close_body(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewPrematureCloseBody(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {}, nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(!c.body_eof);
	assert(c.body_abort);
	assert(!c.request_error);
	assert(c.body_error != nullptr);
	assert(!c.reuse);
}

#endif

/**
 * POST with empty request body.
 */
template<class Connection>
static void
test_post_empty(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_POST, "/foo", {},
			      istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.request_error);
	assert(c.status == HTTP_STATUS_OK ||
	       c.status == HTTP_STATUS_NO_CONTENT);
	assert(c.content_length == nullptr ||
	       strcmp(c.content_length, "0") == 0);

	c.WaitForFirstBodyByte();

	if (c.body_eof) {
		assert(c.available == 0);
	} else {
		assert(c.available == -2);
	}

	assert(c.released);
	assert(!c.body_abort);
	assert(c.body_data == 0);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

#ifdef USE_BUCKETS

template<class Connection>
static void
test_buckets(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available > 0);
	assert(c.body_eof);
	assert(c.body_error == nullptr);
	assert(!c.more_buckets);
	assert(c.total_buckets == (size_t)c.available);
	assert(c.available_after_bucket == 0);
	assert(c.available_after_bucket_partial == 0);
	assert(c.reuse);
}

template<class Connection>
static void
test_buckets_close(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.close_after_buckets = true;

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available > 0);
	assert(!c.body_eof);
	assert(c.body_error == nullptr);
	assert(!c.more_buckets);
	assert(c.total_buckets == (size_t)c.available);
	assert(c.available_after_bucket == 0);
	assert(c.available_after_bucket_partial == 0);
}

#endif

#ifdef ENABLE_PREMATURE_END

template<class Connection>
static void
test_premature_end(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewPrematureEnd(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available > 0);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
}

#endif

#ifdef ENABLE_EXCESS_DATA

template<class Connection>
static void
test_excess_data(Context<Connection> &c) noexcept
{
	c.connection = Connection::NewExcessData(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.content_length == nullptr);
	assert(c.available > 0);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
}

#endif

#ifdef ENABLE_VALID_PREMATURE

template<class Connection>
static void
TestValidPremature(Context<Connection> &c)
{
	c.connection = Connection::NewValidPremature(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
	assert(c.reuse);
}

#endif

#ifdef ENABLE_MALFORMED_PREMATURE

template<class Connection>
static void
TestMalformedPremature(Context<Connection> &c)
{
	c.connection = Connection::NewMalformedPremature(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.available == 1024);
	assert(c.body_data == 0);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
	assert(!c.reuse);
}

#endif

template<class Connection>
static void
TestCancelNop(Context<Connection> &c)
{
	c.connection = Connection::NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_POST, "/foo", {},
			      istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.cancel_ptr.Cancel();

	assert(c.released);
}

template<class Connection>
static void
TestCancelWithFailedSocketGet(Context<Connection> &c)
{
	c.connection = Connection::NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	assert(c.released);
}

template<class Connection>
static void
TestCancelWithFailedSocketPost(Context<Connection> &c)
{
	c.connection = Connection::NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_POST, "/foo", {},
			      istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	assert(c.released);
}

template<class Connection>
static void
TestCloseWithFailedSocketGet(Context<Connection> &c)
{
	c.connection = Connection::NewBlock(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.defer_event.Cancel();

	c.event_loop.Dispatch();

	assert(c.released);
}

template<class Connection>
static void
TestCloseWithFailedSocketPost(Context<Connection> &c)
{
	c.connection = Connection::NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_POST, "/foo", {},
			      istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.released);
	assert(c.status == HTTP_STATUS_OK);
	assert(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.defer_event.Cancel();

	c.event_loop.Dispatch();

	assert(c.released);
}


/*
 * main
 *
 */

template<class Connection>
static void
run_test(void (*test)(Context<Connection> &c)) noexcept
{
	Context<Connection> c;
	test(c);
}

#ifdef USE_BUCKETS

template<class Connection>
static void
run_bucket_test(void (*test)(Context<Connection> &c)) noexcept
{
	Context<Connection> c;
	c.use_buckets = true;
	c.read_after_buckets = true;
	test(c);
}

#endif

template<class Connection>
static void
run_test_and_buckets(void (*test)(Context<Connection> &c)) noexcept
{
	/* regular run */
	run_test(test);

#ifdef USE_BUCKETS
	run_bucket_test(test);
#endif
}

template<class Connection>
static void
run_all_tests() noexcept
{
	run_test(test_empty<Connection>);
	run_test_and_buckets(test_body<Connection>);
	run_test(test_read_body<Connection>);
#ifdef ENABLE_HUGE_BODY
	run_test_and_buckets(test_huge<Connection>);
#endif
	run_test(TestCancelNop<Connection>);
	run_test(test_close_response_body_early<Connection>);
	run_test(test_close_response_body_late<Connection>);
	run_test(test_close_response_body_data<Connection>);
	run_test(test_close_response_body_after<Connection>);
	run_test(test_close_request_body_early<Connection>);
	run_test(test_close_request_body_fail<Connection>);
	run_test(test_data_blocking<Connection>);
	run_test(test_data_blocking2<Connection>);
	run_test(test_body_fail<Connection>);
	run_test(test_head<Connection>);
	run_test(test_head_discard<Connection>);
	run_test(test_head_discard2<Connection>);
	run_test(test_ignored_body<Connection>);
#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY
	run_test(test_close_ignored_request_body<Connection>);
	run_test(test_head_close_ignored_request_body<Connection>);
	run_test(test_close_request_body_eor<Connection>);
	run_test(test_close_request_body_eor2<Connection>);
#endif
#ifdef HAVE_EXPECT_100
	run_test(test_bogus_100<Connection>);
	run_test(test_twice_100<Connection>);
	run_test(test_close_100<Connection>);
#endif
	run_test(test_no_body_while_sending<Connection>);
	run_test(test_hold<Connection>);
#ifdef ENABLE_PREMATURE_CLOSE_HEADERS
	run_test(test_premature_close_headers<Connection>);
#endif
#ifdef ENABLE_PREMATURE_CLOSE_BODY
	run_test_and_buckets(test_premature_close_body<Connection>);
#endif
#ifdef USE_BUCKETS
	run_test(test_buckets<Connection>);
	run_test(test_buckets_close<Connection>);
#endif
#ifdef ENABLE_PREMATURE_END
	run_test_and_buckets(test_premature_end<Connection>);
#endif
#ifdef ENABLE_EXCESS_DATA
	run_test_and_buckets(test_excess_data<Connection>);
#endif
#ifdef ENABLE_VALID_PREMATURE
	run_test_and_buckets(TestValidPremature<Connection>);
#endif
#ifdef ENABLE_MALFORMED_PREMATURE
	run_test_and_buckets(TestMalformedPremature<Connection>);
#endif
	run_test(test_post_empty<Connection>);
	run_test_and_buckets(TestCancelWithFailedSocketGet<Connection>);
	run_test_and_buckets(TestCancelWithFailedSocketPost<Connection>);
	run_test_and_buckets(TestCloseWithFailedSocketGet<Connection>);
	run_test_and_buckets(TestCloseWithFailedSocketPost<Connection>);
}
