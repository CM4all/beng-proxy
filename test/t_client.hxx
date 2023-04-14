// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/Method.hxx"
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
#include "event/DeferEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "PInstance.hxx"
#include "strmap.hxx"
#include "memory/fb_pool.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "AllocatorPtr.hxx"

#ifdef USE_BUCKETS
#include "istream/Bucket.hxx"
#endif

#ifdef HAVE_EXPECT_100
#include "http/Client.hxx"
#endif

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

class ClientConnection {
public:
	virtual ~ClientConnection() noexcept = default;

	virtual void Request(struct pool &pool,
			     Lease &lease,
			     HttpMethod method, const char *uri,
			     StringMap &&headers,
			     UnusedIstreamPtr body,
			     bool expect_100,
			     HttpResponseHandler &handler,
			     CancellablePointer &cancel_ptr) noexcept = 0;

	virtual void InjectSocketFailure() noexcept = 0;
};

struct Instance final : PInstance {
};

struct Context final
	: Cancellable, Lease, HttpResponseHandler, IstreamSink {

	EventLoop &event_loop;

	FineTimerEvent break_timer{event_loop, BIND_THIS_METHOD(OnBreakEvent)};

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
	 * Call EventLoop::Break() as soon as response body data is
	 * received?
	 */
	bool break_data = false;

	/**
	 * Call EventLoop::Break() as soon as response body ends?
	 */
	bool break_eof = false;

	/**
	 * Call EventLoop::Break() as soon as the lease is released?
	 */
	bool break_released = false;

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
	ClientConnection *connection = nullptr;
	bool released = false, reuse, aborted = false;
	HttpStatus status = HttpStatus{};
	std::exception_ptr request_error;

	char *content_length = nullptr;
	off_t available = 0;

	DelayedIstreamControl *delayed = nullptr;

	off_t body_data = 0, consumed_body_data = 0;
	bool body_eof = false, body_closed = false;

	DelayedIstreamControl *request_body = nullptr;
	bool aborted_request_body = false;
	bool close_request_body_early = false, close_request_body_eof = false;
	std::exception_ptr body_error;

#ifdef USE_BUCKETS
	bool use_buckets = false;
	bool more_buckets;
	bool buckets_after_data = false;
	bool read_after_buckets = false, close_after_buckets = false;
	size_t total_buckets;
	off_t available_after_bucket, available_after_bucket_partial;
#endif

	FineTimerEvent read_later_event{event_loop, BIND_THIS_METHOD(OnDeferred)};
	DeferEvent read_defer_event{event_loop, BIND_THIS_METHOD(OnDeferred)};
	bool deferred = false;

	explicit Context(Instance &instance) noexcept
		:event_loop(instance.event_loop),
		parent_pool(NewMajorPool(instance.root_pool, "parent")),
		 pool(pool_new_linear(parent_pool, "test", 16384))
	{
	}

	~Context() noexcept {
		assert(connection == nullptr);

		free(content_length);
		parent_pool.reset();
	}

	using IstreamSink::HasInput;
	using IstreamSink::CloseInput;

	bool WaitingForResponse() const noexcept {
		return status == HttpStatus{} && !request_error;
	}

	void WaitForResponse() noexcept {
		break_response = true;

		if (WaitingForResponse())
			event_loop.Run();

		assert(!WaitingForResponse());

		break_response = false;
	}

	void WaitForFirstBodyByte() noexcept {
		assert(status != HttpStatus{});
		assert(!request_error);

		if (body_data > 0 || !HasInput())
			return;

		ReadBody();

		if (body_data > 0 || !HasInput())
			return;

		break_data = true;
		event_loop.Run();
		break_data = false;
	}

	void WaitForEndOfBody() noexcept {
		if (!HasInput())
			return;

		while (data_blocking > 0) {
			ReadBody();
			if (!HasInput())
				return;
		}

		do {
			ReadBody();
			if (!HasInput())
				return;
		} while (response_body_byte);

		break_eof = true;
		event_loop.Run();
		break_eof = false;

		assert(!HasInput());
	}

	void WaitForEnd() noexcept {
		WaitForResponse();
		WaitForEndOfBody();
	}

	/**
	 * Give the client library another chance to release the
	 * socket/process.  This is a workaround for spurious unit test
	 * failures with the AJP client.
	 */
	void WaitReleased() noexcept {
		if (released)
			return;

		break_released = true;
		event_loop.Run();
		break_released = false;

		assert(released);
	}

	void RunFor(Event::Duration duration) noexcept {
		break_timer.Schedule(duration);
		event_loop.Run();
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

		bool eof;

		if (total_buckets > 0) {
			if (break_data)
				event_loop.Break();

			auto result = input.ConsumeBucketList(total_buckets);
			assert(result.consumed == total_buckets);
			body_data += result.consumed;
			eof = result.eof;
		} else
			eof = !more_buckets;

		available_after_bucket = input.GetAvailable(false);
		available_after_bucket_partial = input.GetAvailable(true);

		if (read_after_buckets)
			input.Read();
		else if (close_after_buckets) {
			body_closed = true;
			CloseInput();
			close_response_body_late = false;
		} else if (eof) {
			CloseInput();
			body_eof = true;
		}
	}
#endif

	void OnBreakEvent() noexcept {
		event_loop.Break();
	}

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
		if (use_buckets && !buckets_after_data)
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

		if (break_released)
			event_loop.Break();

		delete connection;
		connection = nullptr;
		released = true;
		reuse = _reuse;
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

void
Context::Cancel() noexcept
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

std::size_t
Context::OnData(std::span<const std::byte> src) noexcept
{
	if (break_data)
		event_loop.Break();

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

#ifdef USE_BUCKETS
	if (buckets_after_data)
		read_defer_event.Schedule();
#endif

	consumed_body_data += src.size();
	return src.size();
}

void
Context::OnEof() noexcept
{
	if (break_data || break_eof)
		event_loop.Break();

	ClearInput();
	body_eof = true;

	read_later_event.Cancel();
	read_defer_event.Cancel();

	if (close_request_body_eof && !aborted_request_body) {
		request_body->SetError(std::make_exception_ptr(std::runtime_error("close_request_body_eof")));
	}
}

void
Context::OnError(std::exception_ptr ep) noexcept
{
	if (break_data || break_eof)
		event_loop.Break();

	ClearInput();

	read_later_event.Cancel();
	read_defer_event.Cancel();

	assert(!body_error);
	body_error = ep;
}

/*
 * http_response_handler
 *
 */

void
Context::OnHttpResponse(HttpStatus _status, StringMap &&headers,
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
	if (use_buckets && !buckets_after_data) {
		if (available >= 0)
			DoBuckets();
		else {
			/* try again later */
			read_later_event.Schedule(std::chrono::milliseconds(10));
			deferred = true;
		}

		return;
	}
#endif

	if (read_response_body)
		ReadBody();

	if (defer_read_response_body) {
		read_defer_event.Schedule();
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

void
Context::OnHttpError(std::exception_ptr ep) noexcept
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

static void
test_empty(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);
	pool_commit();

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

static void
test_body(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HttpStatus::OK);
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
static void
test_read_body(auto &factory, Context &c) noexcept
{
	c.read_response_body = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
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
static void
test_huge(auto &factory, Context &c) noexcept
{
	c.read_response_body = true;
	c.close_response_body_data = true;
	c.connection = factory.NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.available >= 65536);
	assert(!c.body_eof);
	assert(c.body_data > 0);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

#endif

static void
test_close_response_body_early(auto &factory, Context &c) noexcept
{
	c.close_response_body_early = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	assert(c.available == 6);
	assert(!c.HasInput());
	assert(c.body_data == 0);
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

static void
test_close_response_body_late(auto &factory, Context &c) noexcept
{
	c.close_response_body_late = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	assert(c.available == 6);
	assert(!c.HasInput());
	assert(c.body_data == 0);
	assert(!c.body_eof);
	assert(c.body_closed);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

static void
test_close_response_body_data(auto &factory, Context &c) noexcept
{
	c.close_response_body_data = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HttpStatus::OK);
	assert(!c.request_error);
	assert(c.content_length == nullptr);
	assert(c.available == 6);

	c.WaitForFirstBodyByte();

	assert(c.released);
	assert(!c.HasInput());
	assert(c.body_data == 6);
	assert(!c.body_eof);
	assert(c.body_closed);
	assert(c.body_error == nullptr);
}

static void
test_close_response_body_after(auto &factory, Context &c) noexcept
{
	c.close_response_body_after = 16384;
	c.connection = factory.NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HttpStatus::OK);
	assert(!c.request_error);
	assert(c.content_length == nullptr);
	assert(c.available == 524288);

	c.event_loop.Run();

	assert(c.released);
	assert(!c.HasInput());
	assert(c.body_data >= 16384);
	assert(!c.body_eof);
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

static UnusedIstreamPtr
make_delayed_request_body(Context &c) noexcept
{
	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	delayed.second.cancel_ptr = c;
	c.request_body = &delayed.second;
	return std::move(delayed.first);
}

static void
test_close_request_body_early(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
			      false,
			      c, c.cancel_ptr);

	const std::runtime_error error("fail_request_body_early");
	c.request_body->SetError(std::make_exception_ptr(error));

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus{});
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(c.body_error == nullptr);
	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), error.what()) != nullptr);
}

static void
test_close_request_body_fail(auto &factory, Context &c) noexcept
{
	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	auto request_body =
		NewConcatIstream(*c.pool,
				 istream_head_new(*c.pool, istream_zero_new(*c.pool),
						  4096, false),
				 std::move(delayed.first));

	c.delayed = &delayed.second;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, std::move(request_body)),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
	assert(c.available == -1);
#else
	assert(c.available == HEAD_SIZE);
#endif
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(c.body_error);

	if (!c.request_error) {
		c.request_error = std::exchange(c.body_error, std::exception_ptr());
	}

	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), "delayed_fail") != nullptr);
	assert(c.body_error == nullptr);
}

static void
test_data_blocking(auto &factory, Context &c) noexcept
{
	auto [request_body, approve_control] =
		NewApproveIstream(*c.pool, c.event_loop,
				  istream_head_new(*c.pool,
						   istream_zero_new(*c.pool),
						   65536, false));

	c.data_blocking = 5;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, std::move(request_body)),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.request_error);
	assert(c.status == HttpStatus::OK);
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

		const unsigned old_data_blocking = c.data_blocking;
		c.ReadBody();

		if (c.data_blocking == old_data_blocking)
			c.event_loop.Run();
	}

	approve_control.reset();

	assert(!c.released);
	assert(c.HasInput());
	assert(c.body_data > 0);
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);

	c.CloseInput();

	assert(c.released);
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);

	/* flush all remaining events */
	c.event_loop.Run();
}

/**
 * This produces a closed socket while the HTTP client has data left
 * in the buffer.
 */
static void
test_data_blocking2(auto &factory, Context &c) noexcept
{
	StringMap request_headers;
	request_headers.Add(*c.pool, "connection", "close");

	constexpr size_t body_size = 256;

	c.response_body_byte = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", std::move(request_headers),
			      istream_head_new(*c.pool, istream_zero_new(*c.pool),
					       body_size, true),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(c.status == HttpStatus::OK);
	assert(!c.request_error);

	c.WaitForFirstBodyByte();

	/* the socket is released by now, but the body isn't finished
	   yet */
#ifndef NO_EARLY_RELEASE_SOCKET
	c.WaitReleased();
#endif
	assert(c.content_length == nullptr);
	assert(c.available == body_size);
	assert(c.HasInput());
	assert(!c.body_eof);
	assert(c.consumed_body_data < (off_t)body_size);
	assert(c.body_error == nullptr);

	/* receive the rest of the response body from the buffer */
	c.WaitForEndOfBody();

	assert(c.released);
	assert(c.body_eof);
	assert(c.consumed_body_data == body_size);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

static void
test_body_fail(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMirror(*c.pool, c.event_loop);

	const std::runtime_error error("body_fail");

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_fail_new(*c.pool, std::make_exception_ptr(error))),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.aborted || c.body_error);

	if (c.body_error != nullptr && !c.request_error) {
		c.request_error = std::exchange(c.body_error, std::exception_ptr());
	}

	assert(c.request_error != nullptr);
	assert(strstr(GetFullMessage(c.request_error).c_str(), error.what()) != nullptr);
	assert(c.body_error == nullptr);
}

static void
test_head(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length != nullptr);
	assert(strcmp(c.content_length, "6") == 0);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

/**
 * Send a HEAD request.  The server sends a response body, and the
 * client library is supposed to discard it.
 */
static void
test_head_discard(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

/**
 * Same as test_head_discard(), but uses factory.NewTiny)(*c.pool).
 */
static void
test_head_discard2(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewTiny(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length != nullptr);
	[[maybe_unused]]
		unsigned long content_length = strtoul(c.content_length, nullptr, 10);
	assert(content_length == 5 || content_length == 256);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

static void
test_ignored_body(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_zero_new(*c.pool)),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
	assert(!factory.can_cancel_request_body || c.reuse);
}

#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY

/**
 * Close request body in the response handler (with response body).
 */
static void
test_close_ignored_request_body(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.close_request_body_early = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

/**
 * Close request body in the response handler, method HEAD (no
 * response body).
 */
static void
test_head_close_ignored_request_body(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.close_request_body_early = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::NO_CONTENT);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
static void
test_close_request_body_eor(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewDummy(*c.pool, c.event_loop);
	c.close_request_body_eof = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
static void
test_close_request_body_eor2(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.close_request_body_eof = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      make_delayed_request_body(c),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	assert(!c.HasInput());
	assert(c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

#endif

#ifdef HAVE_EXPECT_100

/**
 * Check if the HTTP client handles "100 Continue" received without
 * announcing the expectation.
 */
static void
test_bogus_100(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewTwice100(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr, false,
			      c, c.cancel_ptr);


	c.event_loop.Run();

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
static void
test_twice_100(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewTwice100(*c.pool, c.event_loop);
	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	delayed.second.cancel_ptr = nullptr;
	c.request_body = &delayed.second;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      std::move(delayed.first),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

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
static void
test_close_100(auto &factory, Context &c) noexcept
{
	auto request_body = istream_delayed_new(*c.pool, c.event_loop);
	request_body.second.cancel_ptr = nullptr;

	c.connection = factory.NewClose100(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      std::move(request_body.first), true,
			      c, c.cancel_ptr);

	c.event_loop.Run();

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
static void
test_no_body_while_sending(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool)),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::NO_CONTENT);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error == nullptr);
}

static void
test_hold(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool)),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(!c.body_error);
	assert(c.body_data == 0);

	c.RunFor(std::chrono::milliseconds{10});

	assert(!c.released);
	assert(c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(!c.body_error);
	assert(c.body_data == 0);

	c.CloseInput();
	c.event_loop.Run();

	assert(c.released);
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(!c.body_error);
	assert(c.body_data == 0);
}

#ifdef ENABLE_PREMATURE_CLOSE_HEADERS

/**
 * The server closes the connection before it finishes sending the
 * response headers.
 */
static void
test_premature_close_headers(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewPrematureCloseHeaders(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus{});
	assert(!c.HasInput());
	assert(!c.body_eof);
	assert(!c.body_error);
	assert(c.request_error != nullptr);
	assert(!c.reuse);
}

#endif

#ifdef ENABLE_PREMATURE_CLOSE_BODY

/**
 * The server closes the connection before it finishes sending the
 * response body.
 */
static void
test_premature_close_body(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewPrematureCloseBody(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {}, nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(!c.body_eof);
	assert(!c.request_error);
	assert(c.body_error != nullptr);
	assert(!c.reuse);
}

#endif

/**
 * POST with empty request body.
 */
static void
test_post_empty(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.request_error);
	assert(c.status == HttpStatus::OK ||
	       c.status == HttpStatus::NO_CONTENT);
	assert(c.content_length == nullptr ||
	       strcmp(c.content_length, "0") == 0);

	c.WaitForFirstBodyByte();

	if (c.body_eof) {
		assert(c.available == 0);
	} else {
		assert(c.available == -2);
	}

	assert(c.released);
	assert(c.body_data == 0);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

#ifdef USE_BUCKETS

static void
test_buckets(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
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

static void
test_buckets_after_data(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.buckets_after_data = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
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

static void
test_buckets_close(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.close_after_buckets = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
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

static void
test_premature_end(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewPrematureEnd(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	assert(c.available > 0);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
}

#endif

#ifdef ENABLE_EXCESS_DATA

static void
test_excess_data(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewExcessData(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	assert(c.available > 0);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
}

#endif

#ifdef ENABLE_VALID_PREMATURE

static void
TestValidPremature(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewValidPremature(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
	assert(c.reuse);
}

#endif

#ifdef ENABLE_MALFORMED_PREMATURE

static void
TestMalformedPremature(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewMalformedPremature(*c.pool, c.event_loop);

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.available == 1024);
	assert(c.body_data == 0);
	assert(!c.body_eof);
	assert(c.body_error != nullptr);
	assert(!c.reuse);
}

#endif

static void
TestCancelNop(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,
			      c, c.cancel_ptr);

	c.cancel_ptr.Cancel();

	assert(c.released);
}

static void
TestCancelWithFailedSocketGet(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	assert(c.released);
}

static void
TestCancelWithFailedSocketPost(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,

			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	assert(c.released);
}

static void
TestCloseWithFailedSocketGet(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewBlock(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.read_later_event.Cancel();
	c.read_defer_event.Cancel();

	c.event_loop.Run();

	assert(c.released);
}

static void
TestCloseWithFailedSocketPost(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,

			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.read_later_event.Cancel();
	c.read_defer_event.Cancel();

	c.event_loop.Run();

	assert(c.released);
}


/*
 * main
 *
 */

template<class Factory>
static void
run_test(Instance &instance, Factory &factory,
	 void (*test)(Factory &factory, Context &c)) noexcept
{
	Context c{instance};
	test(factory, c);
}

#ifdef USE_BUCKETS

template<class Factory>
static void
run_bucket_test(Instance &instance, Factory &factory,
		void (*test)(Factory &factory, Context &c)) noexcept
{
	Context c{instance};
	c.use_buckets = true;
	c.read_after_buckets = true;
	test(factory, c);
}

#endif

template<class Factory>
static void
run_test_and_buckets(Instance &instance, Factory &factory,
		     void (*test)(Factory &factory, Context &c)) noexcept
{
	/* regular run */
	run_test(instance, factory, test);

#ifdef USE_BUCKETS
	run_bucket_test(instance, factory, test);
#endif
}

template<class Factory>
static void
run_all_tests(Instance &instance, Factory &factory) noexcept
{
	run_test(instance, factory, test_empty<Factory>);
	run_test_and_buckets(instance, factory, test_body<Factory>);
	run_test(instance, factory, test_read_body<Factory>);
#ifdef ENABLE_HUGE_BODY
	run_test_and_buckets(instance, factory, test_huge<Factory>);
#endif
	run_test(instance, factory, TestCancelNop<Factory>);
	run_test(instance, factory, test_close_response_body_early<Factory>);
	run_test(instance, factory, test_close_response_body_late<Factory>);
	run_test(instance, factory, test_close_response_body_data<Factory>);
	run_test(instance, factory, test_close_response_body_after<Factory>);
	run_test(instance, factory, test_close_request_body_early<Factory>);
	run_test(instance, factory, test_close_request_body_fail<Factory>);
	run_test(instance, factory, test_data_blocking<Factory>);
	run_test(instance, factory, test_data_blocking2<Factory>);
	run_test(instance, factory, test_body_fail<Factory>);
	run_test(instance, factory, test_head<Factory>);
	run_test(instance, factory, test_head_discard<Factory>);
	run_test(instance, factory, test_head_discard2<Factory>);
	run_test(instance, factory, test_ignored_body<Factory>);
#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY
	run_test(instance, factory, test_close_ignored_request_body<Factory>);
	run_test(instance, factory, test_head_close_ignored_request_body<Factory>);
	run_test(instance, factory, test_close_request_body_eor<Factory>);
	run_test(instance, factory, test_close_request_body_eor2<Factory>);
#endif
#ifdef HAVE_EXPECT_100
	run_test(instance, factory, test_bogus_100<Factory>);
	run_test(instance, factory, test_twice_100<Factory>);
	run_test(instance, factory, test_close_100<Factory>);
#endif
	run_test(instance, factory, test_no_body_while_sending<Factory>);
	run_test(instance, factory, test_hold<Factory>);
#ifdef ENABLE_PREMATURE_CLOSE_HEADERS
	run_test(instance, factory, test_premature_close_headers<Factory>);
#endif
#ifdef ENABLE_PREMATURE_CLOSE_BODY
	run_test_and_buckets(instance, factory, test_premature_close_body<Factory>);
#endif
#ifdef USE_BUCKETS
	run_test(instance, factory, test_buckets<Factory>);
	run_test(instance, factory, test_buckets_after_data<Factory>);
	run_test(instance, factory, test_buckets_close<Factory>);
#endif
#ifdef ENABLE_PREMATURE_END
	run_test_and_buckets(instance, factory, test_premature_end<Factory>);
#endif
#ifdef ENABLE_EXCESS_DATA
	run_test_and_buckets(instance, factory, test_excess_data<Factory>);
#endif
#ifdef ENABLE_VALID_PREMATURE
	run_test_and_buckets(instance, factory, TestValidPremature<Factory>);
#endif
#ifdef ENABLE_MALFORMED_PREMATURE
	run_test_and_buckets(instance, factory, TestMalformedPremature<Factory>);
#endif
	run_test(instance, factory, test_post_empty<Factory>);
	run_test_and_buckets(instance, factory, TestCancelWithFailedSocketGet<Factory>);
	run_test_and_buckets(instance, factory, TestCancelWithFailedSocketPost<Factory>);
	run_test_and_buckets(instance, factory, TestCloseWithFailedSocketGet<Factory>);
	run_test_and_buckets(instance, factory, TestCloseWithFailedSocketPost<Factory>);
}
