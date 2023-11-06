// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "t_client.hxx"
#include "istream/Bucket.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "memory/fb_pool.hxx"

static PoolPtr
NewMajorPool(struct pool &parent, const char *name) noexcept
{
	auto pool = pool_new_dummy(&parent, name);
	pool_set_major(pool);
	return pool;
}

Context::Context(Instance &instance) noexcept
	:event_loop(instance.event_loop),
	 parent_pool(NewMajorPool(instance.root_pool, "parent")),
	 pool(pool_new_linear(parent_pool, "test", 16384))
{
}

Context::~Context() noexcept
{
	assert(connection == nullptr);

	free(content_length);
	parent_pool.reset();
}

void
Context::WaitForResponse() noexcept
{
	break_response = true;

	if (WaitingForResponse())
		event_loop.Run();

	assert(!WaitingForResponse());

	break_response = false;
}

void
Context::WaitForFirstBodyByte() noexcept
{
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

void
Context::WaitForEndOfBody() noexcept
{
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

void
Context::WaitForEnd() noexcept
{
	WaitForResponse();
	WaitForEndOfBody();
}

void
Context::WaitReleased() noexcept
{
	if (released)
		return;

	break_released = true;
	event_loop.Run();
	break_released = false;

	assert(released);
}

void
Context::RunFor(Event::Duration duration) noexcept
{
	break_timer.Schedule(duration);
	event_loop.Run();
}

void
Context::DoBuckets() noexcept {
	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		body_error = std::current_exception();
		return;
	}

	more_buckets = list.HasMore();
	total_buckets = list.GetTotalBufferSize();
	body_data += total_buckets;

	bool eof;
	bool again = false;

	if (total_buckets > 0) {
		if (break_data)
			event_loop.Break();

		std::size_t consume_buckets = total_buckets;

		if (close_after_buckets)
			/* since we want to continue I/O after
			   consuming buckets, let's not
			   consume all */
			--consume_buckets;

		auto result = input.ConsumeBucketList(consume_buckets);
		assert(result.consumed == consume_buckets);
		consumed_body_data += result.consumed;
		eof = result.eof;

		again = result.consumed > 0 && !break_data;
	} else
		eof = !more_buckets;

	available_after_bucket = input.GetAvailable(false);
	available_after_bucket_partial = input.GetAvailable(true);

	if (eof) {
		assert(!close_after_buckets);
		CloseInput();
		body_eof = true;
	} else if (read_after_buckets) {
		input.Read();
	} else if (close_after_buckets) {
		body_closed = true;
		CloseInput();
		close_response_body_late = false;
	} else if (again)
		read_defer_event.Schedule();
}

void
Context::OnDeferred() noexcept
{
	deferred = false;

	if (defer_read_response_body) {
		input.Read();
		return;
	}

	if (use_buckets) {
		if (available < 0)
			available = input.GetAvailable(false);
		DoBuckets();
	} else
		assert(false);
}

void
Context::ReadBody() noexcept
{
	assert(HasInput());

	if (use_buckets && !buckets_after_data)
		DoBuckets();
	else
		input.Read();
}

PutAction
Context::ReleaseLease(PutAction _action) noexcept
{
	assert(connection != nullptr);

	if (break_released)
		event_loop.Break();

	delete connection;
	connection = nullptr;
	released = true;
	lease_action = _action;
	return PutAction::DESTROY;
}

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

IstreamReadyResult
Context::OnIstreamReady() noexcept
{
	if (use_buckets && !read_after_buckets) {
		DoBuckets();
		if (body_error || body_eof || body_closed)
			return IstreamReadyResult::CLOSED;

		return IstreamReadyResult::OK;
	} else
		return IstreamSink::OnIstreamReady();
}

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

	if (buckets_after_data) {
		read_defer_event.Schedule();
		return 0;
	}

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
