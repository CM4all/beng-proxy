// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BufferedResourceLoader.hxx"
#include "RecordingHttpResponseHandler.hxx"
#include "BlockingResourceLoader.hxx"
#include "FailingResourceLoader.hxx"
#include "MirrorResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/InjectIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/Cancellable.hxx"
#include "memory/fb_pool.hxx"
#include "pool/pool.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "PInstance.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(BufferedResourceLoader, Empty)
{
	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			HttpStatus::OK, {},
			nullptr, nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::NO_BODY);
	ASSERT_EQ(handler.body, "");
}

TEST(BufferedResourceLoader, Small)
{
	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			HttpStatus::OK, {},
			NewConcatIstream(handler.pool,
					 istream_string_new(handler.pool, "foo"),
					 istream_string_new(handler.pool, "bar")),
			nullptr,
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::END);
	ASSERT_EQ(handler.body, "foobar");
}

TEST(BufferedResourceLoader, Large)
{
	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	constexpr size_t size = 128 * 1024;
	char *data = (char *)p_malloc(instance.root_pool, size + 1);
	memset(data, 'X', size);
	data[size] = 0;

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			HttpStatus::OK, {},
			istream_string_new(handler.pool, data),
			nullptr,
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::END);
	ASSERT_EQ(handler.body, data);
}

TEST(BufferedResourceLoader, LargeFail)
{
	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;
	FailingResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	constexpr size_t size = 128 * 1024;
	char *data = (char *)p_malloc(instance.root_pool, size + 1);
	memset(data, 'X', size);
	data[size] = 0;

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);

	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			HttpStatus::OK, {},
			istream_string_new(handler.pool, data),
			nullptr,
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::ERROR);
}

TEST(BufferedResourceLoader, EarlyRequestError)
{
	PInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;

	auto inject = istream_inject_new(handler.pool,
					 istream_block_new(handler.pool));

	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::GET, nullptr,
			HttpStatus::OK, {}, std::move(inject.first), nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	inject.second.InjectFault(std::make_exception_ptr(std::runtime_error("error")));

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::ERROR);
	ASSERT_TRUE(handler.error);
}

TEST(BufferedResourceLoader, EarlyResponseError)
{
	PInstance instance;
	FailingResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::GET, nullptr,
			HttpStatus::OK, {}, nullptr, nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::ERROR);
	ASSERT_TRUE(handler.error);
}

TEST(BufferedResourceLoader, CancelEarly)
{
	PInstance instance;
	FailingResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			HttpStatus::OK, {},
			istream_block_new(handler.pool), nullptr,
			handler, cancel_ptr);
	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	cancel_ptr.Cancel();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);
}

TEST(BufferedResourceLoader, CancelNext)
{
	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;
	BlockingResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	constexpr size_t size = 128 * 1024;
	char *data = (char *)p_malloc(instance.root_pool, size + 1);
	memset(data, 'X', size);
	data[size] = 0;

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			HttpStatus::OK, {},
			istream_string_new(handler.pool, data),
			nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	cancel_ptr.Cancel();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);
}
