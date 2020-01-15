/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "fb_pool.hxx"
#include "HttpResponseHandler.hxx"
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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_POST, nullptr,
			HTTP_STATUS_OK, {},
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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_POST, nullptr,
			HTTP_STATUS_OK, {},
			istream_cat_new(handler.pool,
					istream_string_new(handler.pool, "foo"),
					istream_string_new(handler.pool, "bar")),
			nullptr,
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Dispatch();

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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_POST, nullptr,
			HTTP_STATUS_OK, {},
			istream_string_new(handler.pool, data),
			nullptr,
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Dispatch();

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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_POST, nullptr,
			HTTP_STATUS_OK, {},
			istream_string_new(handler.pool, data),
			nullptr,
			handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Dispatch();

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

	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_GET, nullptr,
			HTTP_STATUS_OK, {}, std::move(inject.first), nullptr,
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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_GET, nullptr,
			HTTP_STATUS_OK, {}, nullptr, nullptr,
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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_POST, nullptr,
			HTTP_STATUS_OK, {},
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
	brl.SendRequest(handler.pool, nullptr, 0,
			nullptr, nullptr,
			HTTP_METHOD_POST, nullptr,
			HTTP_STATUS_OK, {},
			istream_string_new(handler.pool, data),
			nullptr,
			handler, cancel_ptr);

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);
	instance.event_loop.LoopNonBlock();
	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);

	cancel_ptr.Cancel();

	ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::WAITING);
}
