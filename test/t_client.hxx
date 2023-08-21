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
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "AllocatorPtr.hxx"

#include <stdexcept>

#include <string.h>

static constexpr size_t HEAD_SIZE = 16384;

struct ClientTestOptions {
	bool have_chunked_request_body = false;
	bool have_expect_100 = false;
	bool can_cancel_request_body = false;
	bool have_content_length_header = true;
	bool enable_buckets = false;
	bool enable_huge_body = true;
	bool enable_close_ignored_request_body = false;
	bool enable_premature_close_headers = false;
	bool enable_premature_close_body = false;
	bool enable_premature_end = false;
	bool enable_excess_data = false;
	bool enable_valid_premature = false;
	bool enable_malformed_premature = false;
	bool no_early_release_socket = false;
};

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

	bool use_buckets = false;
	bool more_buckets;
	bool buckets_after_data = false;
	bool read_after_buckets = false, close_after_buckets = false;
	size_t total_buckets;
	off_t available_after_bucket, available_after_bucket_partial;

	FineTimerEvent read_later_event{event_loop, BIND_THIS_METHOD(OnDeferred)};
	DeferEvent read_defer_event{event_loop, BIND_THIS_METHOD(OnDeferred)};
	bool deferred = false;

	explicit Context(Instance &instance) noexcept;
	~Context() noexcept;

	using IstreamSink::HasInput;

	void CloseInput() noexcept {
		IstreamSink::CloseInput();
		read_later_event.Cancel();
		read_defer_event.Cancel();
	}

	bool WaitingForResponse() const noexcept {
		return status == HttpStatus{} && !request_error;
	}

	void WaitForResponse() noexcept;
	void WaitForFirstBodyByte() noexcept;
	void WaitForEndOfBody() noexcept;
	void WaitForEnd() noexcept;

	/**
	 * Give the client library another chance to release the
	 * socket/process.  This is a workaround for spurious unit test
	 * failures with the AJP client.
	 */
	void WaitReleased() noexcept;

	void RunFor(Event::Duration duration) noexcept;

	void DoBuckets() noexcept;

	void OnBreakEvent() noexcept {
		event_loop.Break();
	}

	void OnDeferred() noexcept;

	void ReadBody() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	void ReleaseLease(bool _reuse) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

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

inline UnusedIstreamPtr
wrap_fake_request_body(struct pool *pool, UnusedIstreamPtr i,
		       const ClientTestOptions &options) noexcept
{
	if (!options.have_chunked_request_body && i.GetAvailable(false) < 0)
		i = istream_head_new(*pool, std::move(i), HEAD_SIZE, true);

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
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
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
				 istream_head_new(c.pool, istream_zero_new(*c.pool),
						  4096, false),
				 std::move(delayed.first));

	c.delayed = &delayed.second;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, std::move(request_body),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	assert(c.released);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	if (factory.options.have_chunked_request_body) {
		assert(c.available == -1);
	} else {
		assert(c.available == HEAD_SIZE);
	}
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
			      wrap_fake_request_body(c.pool, std::move(request_body),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	assert(!c.request_error);
	assert(c.status == HttpStatus::OK);
	assert(c.content_length == nullptr);
	if (factory.options.have_chunked_request_body) {
		assert(c.available == -1);
	} else {
		assert(c.available == HEAD_SIZE);
	}
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
	if (!factory.options.no_early_release_socket)
		c.WaitReleased();
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
			      wrap_fake_request_body(c.pool, istream_fail_new(*c.pool, std::make_exception_ptr(error)),
						     factory.options),
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
			      wrap_fake_request_body(c.pool, istream_zero_new(*c.pool),
						     factory.options),
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
	assert(!factory.options.can_cancel_request_body || c.reuse);
}

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
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
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
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
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
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
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

/**
 * Check if the HTTP client handles "100 Continue" received without
 * announcing the expectation.
 */
template<typename Factory>
static void
test_bogus_100(Factory &factory, Context &c) noexcept
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

	const auto *e = FindNested<typename Factory::Error>(c.request_error);
	(void)e;
	assert(e != nullptr);
	assert(e->GetCode() == Factory::ErrorCode::UNSPECIFIED);

	assert(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
}

/**
 * Check if the HTTP client handles "100 Continue" received twice
 * well.
 */
template<typename Factory>
static void
test_twice_100(Factory &factory, Context &c) noexcept
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

	const auto *e = FindNested<typename Factory::Error>(c.request_error);
	(void)e;
	assert(e != nullptr);
	assert(e->GetCode() == Factory::ErrorCode::UNSPECIFIED);

	assert(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
}

/**
 * The server sends "100 Continue" and closes the socket.
 */
template<typename Factory>
static void
test_close_100(Factory &factory, Context &c) noexcept
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

/**
 * Receive an empty response from the server while still sending the
 * request body.
 */
template<typename Factory>
static void
test_no_body_while_sending(Factory &factory, Context &c) noexcept
{
	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool),
						     factory.options),
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
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool),
						     factory.options),
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
	if (factory.options.have_content_length_header) {
		assert(c.available > 0);
		assert(c.body_eof);
		assert(c.body_error == nullptr);
		assert(!c.more_buckets);
		assert(c.total_buckets == (size_t)c.available);
		assert(c.available_after_bucket == 0);
	}
	assert(c.available_after_bucket_partial == 0);
	assert(c.reuse);
}

static void
test_buckets_chunked(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewDummy(*c.pool, c.event_loop);
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
	if (factory.options.have_content_length_header) {
		assert(c.body_eof);
		assert(c.body_error == nullptr);
		assert(!c.more_buckets);
		assert(c.total_buckets > 0);
		assert(c.available_after_bucket == 0);
	}
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
	if (factory.options.have_content_length_header) {
		assert(c.available > 0);
		assert(!c.more_buckets);
		assert(c.total_buckets == (size_t)c.available);
		assert(c.available_after_bucket == 0);
	}
	assert(c.body_eof);
	assert(c.body_error == nullptr);
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
	if (factory.options.have_content_length_header) {
		assert(c.available > 0);
	}
	assert(!c.body_eof);
	assert(c.body_error == nullptr);
	assert(!c.more_buckets);
	assert(c.total_buckets == (size_t)c.available);
	assert(c.available_after_bucket == 1);
	assert(c.available_after_bucket_partial == 1);
}

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

template<class Factory>
static void
run_test_and_buckets(Instance &instance, Factory &factory,
		     void (*test)(Factory &factory, Context &c)) noexcept
{
	/* regular run */
	run_test(instance, factory, test);

	if (factory.options.enable_buckets)
		run_bucket_test(instance, factory, test);
}

template<class Factory>
static void
run_all_tests(Instance &instance, Factory &factory) noexcept
{
	run_test(instance, factory, test_empty<Factory>);
	run_test_and_buckets(instance, factory, test_body<Factory>);
	run_test(instance, factory, test_read_body<Factory>);
	if constexpr (factory.options.enable_huge_body)
		run_test_and_buckets(instance, factory, test_huge<Factory>);
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
	if constexpr (factory.options.enable_close_ignored_request_body) {
		run_test(instance, factory, test_close_ignored_request_body<Factory>);
		run_test(instance, factory, test_head_close_ignored_request_body<Factory>);
		run_test(instance, factory, test_close_request_body_eor<Factory>);
		run_test(instance, factory, test_close_request_body_eor2<Factory>);
	}
	if constexpr (factory.options.have_expect_100) {
		run_test(instance, factory, test_bogus_100<Factory>);
		run_test(instance, factory, test_twice_100<Factory>);
		run_test(instance, factory, test_close_100<Factory>);
	}
	run_test(instance, factory, test_no_body_while_sending<Factory>);
	run_test(instance, factory, test_hold<Factory>);
	if constexpr (factory.options.enable_premature_close_headers)
		run_test(instance, factory, test_premature_close_headers<Factory>);
	if constexpr (factory.options.enable_premature_close_body)
		run_test_and_buckets(instance, factory, test_premature_close_body<Factory>);
	if constexpr (factory.options.enable_buckets) {
		run_test(instance, factory, test_buckets<Factory>);
		run_test(instance, factory, test_buckets_chunked<Factory>);
		run_test(instance, factory, test_buckets_after_data<Factory>);
		run_test(instance, factory, test_buckets_close<Factory>);
	}
	if constexpr (factory.options.enable_premature_end)
		run_test_and_buckets(instance, factory, test_premature_end<Factory>);
	if constexpr (factory.options.enable_excess_data)
		run_test_and_buckets(instance, factory, test_excess_data<Factory>);
	if constexpr (factory.options.enable_valid_premature)
		run_test_and_buckets(instance, factory, TestValidPremature<Factory>);
	if constexpr (factory.options.enable_malformed_premature)
		run_test_and_buckets(instance, factory, TestMalformedPremature<Factory>);
	run_test(instance, factory, test_post_empty<Factory>);
	run_test_and_buckets(instance, factory, TestCancelWithFailedSocketGet<Factory>);
	run_test_and_buckets(instance, factory, TestCancelWithFailedSocketPost<Factory>);
	run_test_and_buckets(instance, factory, TestCloseWithFailedSocketGet<Factory>);
	run_test_and_buckets(instance, factory, TestCloseWithFailedSocketPost<Factory>);
}
