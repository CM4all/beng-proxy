// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
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
#include "strmap.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>
#include <gtest/gtest-typed-test.h>

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

template<typename T>
class ClientTest : public ::testing::Test {
};

TYPED_TEST_CASE_P(ClientTest);

struct Instance final : TestInstance {
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

TYPED_TEST_P(ClientTest, Empty)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);
	pool_commit();

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::NO_CONTENT);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

TYPED_TEST_P(ClientTest, Body)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 6);

	c.WaitForFirstBodyByte();
	c.WaitReleased();

	EXPECT_TRUE(c.released);
	EXPECT_TRUE(c.body_eof);
	EXPECT_EQ(c.body_data, 6);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

TYPED_TEST_P(ClientTest, BodyBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 6);

	c.WaitForFirstBodyByte();
	c.WaitReleased();

	EXPECT_TRUE(c.released);
	EXPECT_TRUE(c.body_eof);
	EXPECT_EQ(c.body_data, 6);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

/**
 * Call istream_read() on the response body from inside the response
 * callback.
 */
TYPED_TEST_P(ClientTest, ReadBody)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.read_response_body = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 6);
	EXPECT_TRUE(c.body_eof);
	EXPECT_EQ(c.body_data, 6);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

/**
 * A huge response body with declared Content-Length.
 */
TYPED_TEST_P(ClientTest, Huge)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_huge_body)
		GTEST_SKIP();

	Context c{instance};

	c.read_response_body = true;
	c.close_response_body_data = true;
	c.connection = factory.NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.available >= 65536);
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.body_data > 0);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, HugeBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_huge_body)
		GTEST_SKIP();

	Context c{instance};

	c.use_buckets = true;
	c.read_after_buckets = true;

	c.read_response_body = true;
	c.close_response_body_data = true;
	c.connection = factory.NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.available >= 65536);
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.body_data > 0);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, CancelNop)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,
			      c, c.cancel_ptr);

	c.cancel_ptr.Cancel();

	/* let ThreadSocketFilter::postponed_destroy finish */
	c.event_loop.Run();

	EXPECT_TRUE(c.released);
}

TYPED_TEST_P(ClientTest, CloseResponseBodyEarly)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.close_response_body_early = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 6);
	EXPECT_FALSE(c.HasInput());
	EXPECT_EQ(c.body_data, 0);
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, CloseResponseBodyLate)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.close_response_body_late = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 6);
	EXPECT_FALSE(c.HasInput());
	EXPECT_EQ(c.body_data, 0);
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.body_closed);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, CloseResponseBodyData)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.close_response_body_data = true;
	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 6);

	c.WaitForFirstBodyByte();

	EXPECT_TRUE(c.released);
	EXPECT_FALSE(c.HasInput());
	EXPECT_EQ(c.body_data, 6);
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.body_closed);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, CloseResponseBodyAfter)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.close_response_body_after = 16384;
	c.connection = factory.NewHuge(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, 524288);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_FALSE(c.HasInput());
	EXPECT_TRUE(c.body_data >= 16384);
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.body_closed);
	EXPECT_EQ(c.body_error, nullptr);
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

TYPED_TEST_P(ClientTest, CloseRequestBodyEarly)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

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

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus{});
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_NE(c.request_error, nullptr);
	EXPECT_NE(strstr(GetFullMessage(c.request_error).c_str(), error.what()), nullptr);
}

TYPED_TEST_P(ClientTest, CloseRequestBodyFail)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

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

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	if (factory.options.have_chunked_request_body) {
		EXPECT_EQ(c.available, -1);
	} else {
		EXPECT_EQ(c.available, HEAD_SIZE);
	}
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.body_error);

	if (!c.request_error) {
		c.request_error = std::exchange(c.body_error, std::exception_ptr());
	}

	EXPECT_NE(c.request_error, nullptr);
	EXPECT_NE(strstr(GetFullMessage(c.request_error).c_str(), "delayed_fail"), nullptr);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, DataBlocking)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

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

	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	if (factory.options.have_chunked_request_body) {
		EXPECT_EQ(c.available, -1);
	} else {
		EXPECT_EQ(c.available, HEAD_SIZE);
	}
	EXPECT_TRUE(c.HasInput());
	EXPECT_FALSE(c.released);

	approve_control->Approve(16);

	while (c.data_blocking > 0) {
		EXPECT_TRUE(c.HasInput());

		const unsigned old_data_blocking = c.data_blocking;
		c.ReadBody();

		if (c.data_blocking == old_data_blocking)
			c.event_loop.Run();
	}

	approve_control.reset();

	EXPECT_FALSE(c.released);
	EXPECT_TRUE(c.HasInput());
	EXPECT_TRUE(c.body_data > 0);
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);

	c.CloseInput();

	EXPECT_TRUE(c.released);
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);

	/* flush all remaining events */
	c.event_loop.Run();
}

/**
 * This produces a closed socket while the HTTP client has data left
 * in the buffer.
 */
TYPED_TEST_P(ClientTest, DataBlocking2)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

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

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_FALSE(c.request_error);

	c.WaitForFirstBodyByte();

	/* the socket is released by now, but the body isn't finished
	   yet */
	if (!factory.options.no_early_release_socket)
		c.WaitReleased();
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_EQ(c.available, body_size);
	EXPECT_TRUE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.consumed_body_data < (off_t)body_size);
	EXPECT_EQ(c.body_error, nullptr);

	/* receive the rest of the response body from the buffer */
	c.WaitForEndOfBody();

	EXPECT_TRUE(c.released);
	EXPECT_TRUE(c.body_eof);
	EXPECT_EQ(c.consumed_body_data, body_size);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, BodyFail)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMirror(*c.pool, c.event_loop);

	const std::runtime_error error("body_fail");

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_fail_new(*c.pool, std::make_exception_ptr(error)),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_TRUE(c.aborted || c.body_error);

	if (c.body_error != nullptr && !c.request_error) {
		c.request_error = std::exchange(c.body_error, std::exception_ptr());
	}

	EXPECT_NE(c.request_error, nullptr);
	EXPECT_NE(strstr(GetFullMessage(c.request_error).c_str(), error.what()), nullptr);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, Head)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      istream_string_new(*c.pool, "foobar"),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_NE(c.content_length, nullptr);
	EXPECT_EQ(strcmp(c.content_length, "6"), 0);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

/**
 * Send a HEAD request.  The server sends a response body, and the
 * client library is supposed to discard it.
 */
TYPED_TEST_P(ClientTest, HeadDiscard)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

/**
 * Same as test_head_discard(), but uses factory.NewTiny)(*c.pool).
 */
TYPED_TEST_P(ClientTest, HeadDiscard2)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewTiny(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_NE(c.content_length, nullptr);
	[[maybe_unused]]
		unsigned long content_length = strtoul(c.content_length, nullptr, 10);
	EXPECT_TRUE(content_length == 5 || content_length == 256);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, IgnoredBody)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_zero_new(*c.pool),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::NO_CONTENT);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(!factory.options.can_cancel_request_body || c.reuse);
}

/**
 * Close request body in the response handler (with response body).
 */
TYPED_TEST_P(ClientTest, CloseIgnoredRequestBody)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_close_ignored_request_body)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.close_request_body_early = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::NO_CONTENT);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

/**
 * Close request body in the response handler, method HEAD (no
 * response body).
 */
TYPED_TEST_P(ClientTest, HeadCloseIgnoredRequestBody)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_close_ignored_request_body)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.close_request_body_early = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::HEAD, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::NO_CONTENT);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
TYPED_TEST_P(ClientTest, CloseRequestBodyEof)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_close_ignored_request_body)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewDummy(*c.pool, c.event_loop);
	c.close_request_body_eof = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, make_delayed_request_body(c),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_FALSE(c.HasInput());
	EXPECT_TRUE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
TYPED_TEST_P(ClientTest, CloseRequestBodyEof2)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_close_ignored_request_body)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.close_request_body_eof = true;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      make_delayed_request_body(c),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	EXPECT_FALSE(c.HasInput());
	EXPECT_TRUE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

/**
 * Check if the HTTP client handles "100 Continue" received without
 * announcing the expectation.
 */
TYPED_TEST_P(ClientTest, Bogus100)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.have_expect_100) {
		Context c{instance};

		c.connection = factory.NewTwice100(*c.pool, c.event_loop);
		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr, false,
				      c, c.cancel_ptr);


		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_TRUE(c.aborted);
		EXPECT_TRUE(c.request_error);

		const auto *e = FindNested<typename TypeParam::Error>(c.request_error);
		(void)e;
		EXPECT_NE(e, nullptr);
		EXPECT_EQ(e->GetCode(), TypeParam::ErrorCode::UNSPECIFIED);

		EXPECT_NE(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100"), nullptr);
		EXPECT_EQ(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	} else
		GTEST_SKIP();

}

/**
 * Check if the HTTP client handles "100 Continue" received twice
 * well.
 */
TYPED_TEST_P(ClientTest, Twice100)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.have_expect_100) {
		Context c{instance};

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

		EXPECT_TRUE(c.released);
		EXPECT_TRUE(c.aborted);
		EXPECT_TRUE(c.request_error);

		const auto *e = FindNested<typename TypeParam::Error>(c.request_error);
		(void)e;
		EXPECT_NE(e, nullptr);
		EXPECT_EQ(e->GetCode(), TypeParam::ErrorCode::UNSPECIFIED);

		EXPECT_NE(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100"), nullptr);
		EXPECT_EQ(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	} else
		GTEST_SKIP();

}

/**
 * The server sends "100 Continue" and closes the socket.
 */
TYPED_TEST_P(ClientTest, Close100)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.have_expect_100) {
		Context c{instance};

		auto request_body = istream_delayed_new(*c.pool, c.event_loop);
		request_body.second.cancel_ptr = nullptr;

		c.connection = factory.NewClose100(*c.pool, c.event_loop);
		c.connection->Request(c.pool, c,
				      HttpMethod::POST, "/foo", {},
				      std::move(request_body.first), true,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_TRUE(c.aborted);
		EXPECT_NE(c.request_error, nullptr);
		EXPECT_TRUE(strstr(GetFullMessage(c.request_error).c_str(), "closed the socket prematurely") != nullptr ||
		       /* the following two errors are not the primary error,
			  but sometimes occur depending on the timing: */
		       strstr(GetFullMessage(c.request_error).c_str(), "Connection reset by peer") != nullptr ||
		       strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
		EXPECT_EQ(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	} else
		GTEST_SKIP();

}

/**
 * Receive an empty response from the server while still sending the
 * request body.
 */
TYPED_TEST_P(ClientTest, NoBodyWhileSending)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewNull(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::NO_CONTENT);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.body_error, nullptr);
}

TYPED_TEST_P(ClientTest, Hold)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      wrap_fake_request_body(c.pool, istream_block_new(*c.pool),
						     factory.options),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_FALSE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.body_data, 0);

	c.RunFor(std::chrono::milliseconds{10});

	EXPECT_FALSE(c.released);
	EXPECT_TRUE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.body_data, 0);

	c.CloseInput();
	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_FALSE(c.HasInput());
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.body_data, 0);
}

/**
 * The server closes the connection before it finishes sending the
 * response headers.
 */
TYPED_TEST_P(ClientTest, PrematureCloseHeaders)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_premature_close_headers) {
		GTEST_SKIP();
	} else {
		Context c{instance};

		c.connection = factory.NewPrematureCloseHeaders(*c.pool, c.event_loop);
		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus{});
		EXPECT_FALSE(c.HasInput());
		EXPECT_FALSE(c.body_eof);
		EXPECT_FALSE(c.body_error);
		EXPECT_NE(c.request_error, nullptr);
		EXPECT_FALSE(c.reuse);
	}
}

/**
 * The server closes the connection before it finishes sending the
 * response body.
 */
TYPED_TEST_P(ClientTest, PrematureCloseBody)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_premature_close_body) {
		GTEST_SKIP();
	} else {
		Context c{instance};

		c.connection = factory.NewPrematureCloseBody(*c.pool, c.event_loop);
		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {}, nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_FALSE(c.body_eof);
		EXPECT_FALSE(c.request_error);
		EXPECT_NE(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	}
}

TYPED_TEST_P(ClientTest, PrematureCloseBodyBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_premature_close_body) {
		GTEST_SKIP();
	} else {
		Context c{instance};

		c.use_buckets = true;
		c.read_after_buckets = true;

		c.connection = factory.NewPrematureCloseBody(*c.pool, c.event_loop);
		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {}, nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_FALSE(c.body_eof);
		EXPECT_FALSE(c.request_error);
		EXPECT_NE(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	}
}

/**
 * POST with empty request body.
 */
TYPED_TEST_P(ClientTest, PostEmpty)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,
			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_FALSE(c.request_error);
	EXPECT_TRUE(c.status == HttpStatus::OK ||
	       c.status == HttpStatus::NO_CONTENT);
	EXPECT_TRUE(c.content_length == nullptr ||
	       strcmp(c.content_length, "0") == 0);

	c.WaitForFirstBodyByte();

	if (c.body_eof) {
		EXPECT_EQ(c.available, 0);
	} else {
		EXPECT_EQ(c.available, -2);
	}

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.body_data, 0);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_TRUE(c.reuse);
}

TYPED_TEST_P(ClientTest, Buckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_buckets)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	if (factory.options.have_content_length_header) {
		EXPECT_TRUE(c.available > 0);
		EXPECT_TRUE(c.body_eof);
		EXPECT_EQ(c.body_error, nullptr);
		EXPECT_FALSE(c.more_buckets);
		EXPECT_EQ(c.total_buckets, (size_t)c.available);
		EXPECT_EQ(c.available_after_bucket, 0);
	}
	EXPECT_EQ(c.available_after_bucket_partial, 0);
	EXPECT_TRUE(c.reuse);
}

TYPED_TEST_P(ClientTest, BucketsChunked)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_buckets)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewDummy(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.buckets_after_data = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	if (factory.options.have_content_length_header) {
		EXPECT_TRUE(c.body_eof);
		EXPECT_EQ(c.body_error, nullptr);
		EXPECT_FALSE(c.more_buckets);
		EXPECT_TRUE(c.total_buckets > 0);
		EXPECT_EQ(c.available_after_bucket, 0);
	}
	EXPECT_EQ(c.available_after_bucket_partial, 0);
	EXPECT_TRUE(c.reuse);
}

TYPED_TEST_P(ClientTest, BucketsAfterData)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_buckets)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.buckets_after_data = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	if (factory.options.have_content_length_header) {
		EXPECT_TRUE(c.available > 0);
		EXPECT_FALSE(c.more_buckets);
		EXPECT_EQ(c.total_buckets, (size_t)c.available);
		EXPECT_EQ(c.available_after_bucket, 0);
	}
	EXPECT_TRUE(c.body_eof);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_EQ(c.available_after_bucket_partial, 0);
	EXPECT_TRUE(c.reuse);
}

TYPED_TEST_P(ClientTest, BucketsClose)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	if constexpr (!factory.options.enable_buckets)
		GTEST_SKIP();

	Context c{instance};

	c.connection = factory.NewFixed(*c.pool, c.event_loop);
	c.use_buckets = true;
	c.close_after_buckets = true;

	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.content_length, nullptr);
	if (factory.options.have_content_length_header) {
		EXPECT_TRUE(c.available > 0);
	}
	EXPECT_FALSE(c.body_eof);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_FALSE(c.more_buckets);
	EXPECT_EQ(c.total_buckets, (size_t)c.available);
	EXPECT_EQ(c.available_after_bucket, 1);
	EXPECT_EQ(c.available_after_bucket_partial, 1);
}

TYPED_TEST_P(ClientTest, PrematureEnd)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_premature_end) {
		Context c{instance};
		c.connection = factory.NewPrematureEnd(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_EQ(c.content_length, nullptr);
		EXPECT_TRUE(c.available > 0);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, PrematureEndBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_premature_end) {
		Context c{instance};
		c.use_buckets = true;
		c.read_after_buckets = true;

		c.connection = factory.NewPrematureEnd(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_EQ(c.content_length, nullptr);
		EXPECT_TRUE(c.available > 0);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, ExcessData)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_excess_data) {
		Context c{instance};

		c.connection = factory.NewExcessData(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_EQ(c.content_length, nullptr);
		EXPECT_TRUE(c.available > 0);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, ExcessDataBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_excess_data) {
		Context c{instance};
		c.use_buckets = true;
		c.read_after_buckets = true;

		c.connection = factory.NewExcessData(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_EQ(c.content_length, nullptr);
		EXPECT_TRUE(c.available > 0);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, ValidPremature)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_valid_premature) {
		Context c{instance};

		c.connection = factory.NewValidPremature(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
		EXPECT_TRUE(c.reuse);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, ValidPrematureBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_valid_premature) {
		Context c{instance};
		c.use_buckets = true;
		c.read_after_buckets = true;

		c.connection = factory.NewValidPremature(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
		EXPECT_TRUE(c.reuse);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, MalformedPremature)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_malformed_premature) {
		Context c{instance};

		c.connection = factory.NewMalformedPremature(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_EQ(c.available, 1024);
		EXPECT_EQ(c.body_data, 0);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, MalformedPrematureBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};

	if constexpr (factory.options.enable_malformed_premature) {
		Context c{instance};
		c.use_buckets = true;
		c.read_after_buckets = true;

		c.connection = factory.NewMalformedPremature(*c.pool, c.event_loop);

		c.connection->Request(c.pool, c,
				      HttpMethod::GET, "/foo", {},
				      nullptr,
				      false,
				      c, c.cancel_ptr);

		c.event_loop.Run();

		EXPECT_TRUE(c.released);
		EXPECT_EQ(c.status, HttpStatus::OK);
		EXPECT_EQ(c.available, 1024);
		EXPECT_EQ(c.body_data, 0);
		EXPECT_FALSE(c.body_eof);
		EXPECT_NE(c.body_error, nullptr);
		EXPECT_FALSE(c.reuse);
	} else
		GTEST_SKIP();
}

TYPED_TEST_P(ClientTest, CancelWithFailedSocketGet)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	EXPECT_TRUE(c.released);

	/* let ThreadSocketFilter::postponed_destroy finish */
	c.event_loop.Run();
}

TYPED_TEST_P(ClientTest, CancelWithFailedSocketGetBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	EXPECT_TRUE(c.released);

	/* let ThreadSocketFilter::postponed_destroy finish */
	c.event_loop.Run();
}

TYPED_TEST_P(ClientTest, CancelWithFailedSocketPost)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,

			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	EXPECT_TRUE(c.released);

	/* let ThreadSocketFilter::postponed_destroy finish */
	c.event_loop.Run();
}

TYPED_TEST_P(ClientTest, CancelWithFailedSocketPostBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection = factory.NewNop(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,

			      c, c.cancel_ptr);

	c.connection->InjectSocketFailure();
	c.cancel_ptr.Cancel();

	EXPECT_TRUE(c.released);

	/* let ThreadSocketFilter::postponed_destroy finish */
	c.event_loop.Run();
}

TYPED_TEST_P(ClientTest, CloseWithFailedSocketGet)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewBlock(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_FALSE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.read_later_event.Cancel();
	c.read_defer_event.Cancel();

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
}

TYPED_TEST_P(ClientTest, CloseWithFailedSocketGetBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection = factory.NewBlock(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_FALSE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.read_later_event.Cancel();
	c.read_defer_event.Cancel();

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
}

TYPED_TEST_P(ClientTest, CloseWithFailedSocketPost)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,

			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_FALSE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.read_later_event.Cancel();
	c.read_defer_event.Cancel();

	c.event_loop.Run();

	EXPECT_TRUE(c.released);

	/* let ThreadSocketFilter::postponed_destroy finish */
	c.event_loop.Run();
}

TYPED_TEST_P(ClientTest, CloseWithFailedSocketPostBuckets)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};
	c.use_buckets = true;
	c.read_after_buckets = true;

	c.connection = factory.NewHold(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/foo", {},
			      istream_null_new(*c.pool),
			      false,

			      c, c.cancel_ptr);

	c.WaitForResponse();

	EXPECT_FALSE(c.released);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.HasInput());

	c.connection->InjectSocketFailure();
	c.CloseInput();
	c.read_later_event.Cancel();
	c.read_defer_event.Cancel();

	c.event_loop.Run();

	EXPECT_TRUE(c.released);
}

REGISTER_TYPED_TEST_CASE_P(ClientTest,
			   Empty,
			   Body,
			   BodyBuckets,
			   ReadBody,
			   Huge,
			   HugeBuckets,
			   CancelNop,
			   CloseResponseBodyEarly,
			   CloseResponseBodyLate,
			   CloseResponseBodyData,
			   CloseResponseBodyAfter,
			   CloseRequestBodyEarly,
			   CloseRequestBodyFail,
			   DataBlocking,
			   DataBlocking2,
			   BodyFail,
			   Head,
			   HeadDiscard,
			   HeadDiscard2,
			   IgnoredBody,
			   CloseIgnoredRequestBody,
			   HeadCloseIgnoredRequestBody,
			   CloseRequestBodyEof,
			   CloseRequestBodyEof2,
			   Bogus100,
			   Twice100,
			   Close100,
			   NoBodyWhileSending,
			   Hold,
			   PrematureCloseHeaders,
			   PrematureCloseBody,
			   PrematureCloseBodyBuckets,
			   PostEmpty,
			   Buckets,
			   BucketsChunked,
			   BucketsAfterData,
			   BucketsClose,
			   PrematureEnd,
			   PrematureEndBuckets,
			   ExcessData,
			   ExcessDataBuckets,
			   ValidPremature,
			   ValidPrematureBuckets,
			   MalformedPremature,
			   MalformedPrematureBuckets,
			   CancelWithFailedSocketGet,
			   CancelWithFailedSocketGetBuckets,
			   CancelWithFailedSocketPost,
			   CancelWithFailedSocketPostBuckets,
			   CloseWithFailedSocketGet,
			   CloseWithFailedSocketGetBuckets,
			   CloseWithFailedSocketPost,
			   CloseWithFailedSocketPostBuckets);
