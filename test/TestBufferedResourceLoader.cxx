// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "RecordingHttpResponseHandler.hxx"
#include "ResourceAddress.hxx"
#include "http/rl/BufferedResourceLoader.hxx"
#include "http/rl/BlockingResourceLoader.hxx"
#include "http/rl/FailingResourceLoader.hxx"
#include "http/rl/MirrorResourceLoader.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/InjectIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/Cancellable.hxx"
#include "pool/pool.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(BufferedResourceLoader, Empty)
{
	TestInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			{},
			nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::NO_BODY);
	ASSERT_EQ(handler.body, "");
}

TEST(BufferedResourceLoader, Small)
{
	TestInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			{},
			NewConcatIstream(handler.pool,
					 istream_string_new(handler.pool, "foo"),
					 istream_string_new(handler.pool, "bar")),
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::END);
	ASSERT_EQ(handler.body, "foobar");
}

TEST(BufferedResourceLoader, Large)
{
	TestInstance instance;
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
			{},
			istream_string_new(handler.pool, data),
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::END);
	ASSERT_EQ(handler.body, data);
}

TEST(BufferedResourceLoader, LargeFail)
{
	TestInstance instance;
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
			{},
			istream_string_new(handler.pool, data),
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::ERROR);
}

TEST(BufferedResourceLoader, EarlyRequestError)
{
	TestInstance instance;
	MirrorResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;

	auto inject = istream_inject_new(handler.pool,
					 istream_block_new(handler.pool));

	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::GET, nullptr,
			{}, std::move(inject.first),
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	inject.second.InjectFault(std::make_exception_ptr(std::runtime_error("error")));

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::ERROR);
	ASSERT_TRUE(handler.error);
}

TEST(BufferedResourceLoader, EarlyResponseError)
{
	TestInstance instance;
	FailingResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::GET, nullptr,
			{}, nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::ERROR);
	ASSERT_TRUE(handler.error);
}

TEST(BufferedResourceLoader, CancelEarly)
{
	TestInstance instance;
	FailingResourceLoader rl;
	BufferedResourceLoader brl(instance.event_loop, rl, nullptr);

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);
	CancellablePointer cancel_ptr;
	brl.SendRequest(handler.pool, nullptr, {},
			HttpMethod::POST, nullptr,
			{},
			istream_block_new(handler.pool),
			handler, cancel_ptr);
	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	cancel_ptr.Cancel();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);
}

TEST(BufferedResourceLoader, CancelNext)
{
	TestInstance instance;
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
			{},
			istream_string_new(handler.pool, data),
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	cancel_ptr.Cancel();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);
}
