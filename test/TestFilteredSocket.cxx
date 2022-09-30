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

#include "EchoSocket.hxx"
#include "TestBufferedSocketHandler.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/Lease.hxx"
#include "fs/NopSocketFilter.hxx"
#include "fs/NopThreadSocketFilter.hxx"
#include "fs/ApproveThreadSocketFilter.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread/Pool.hxx"
#include "memory/fb_pool.hxx"
#include "event/Loop.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "lease.hxx"

#include <gtest/gtest.h>

#include <memory>

#include <sys/socket.h>

using std::string_view_literals::operator""sv;

struct Instance final {
	EventLoop event_loop;

	[[no_unique_address]]
	const ScopeFbPoolInit fb_pool_init;

	Instance() noexcept {
		/* keep the eventfd unregistered if the ThreadQueue is
		   empty, so EventLoop::Dispatch() doesn't keep
		   running after the HTTP request has completed */
		thread_pool_set_volatile();
	}

	~Instance() noexcept {
		/* before all workers are shut down, let the EventLoop
		  dispatch pending events, to account for
		  ThreadSocketFilter instances which are in
		  "postponed_destroy" state */
		// TODO manually cancel "postponed_destroy" on shutdown
		event_loop.Dispatch();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	auto NewThreadSocketFilter(std::unique_ptr<ThreadSocketFilterHandler> handler) {
		return SocketFilterPtr{
			new ThreadSocketFilter(event_loop,
					       thread_pool_get_queue(event_loop),
					       std::move(handler))
		};
	}
};

static std::pair<UniqueSocketDescriptor, std::unique_ptr<EchoSocket>>
NewEchoSocket(EventLoop &event_loop, SocketFilterPtr _filter={})
{
	UniqueSocketDescriptor a, b;
	if (!UniqueSocketDescriptor::CreateSocketPairNonBlock(AF_LOCAL,
							      SOCK_STREAM, 0,
							      a, b))
		throw MakeErrno("socketpair() failed");

	return {
		std::move(a),
		std::make_unique<EchoSocket>(event_loop, std::move(b),
					     FD_SOCKET, std::move(_filter)),
	};
}

TEST(FilteredSocket, NullFilter)
{
	Instance instance;

	auto [s, echo] = NewEchoSocket(instance.event_loop);

	FilteredSocket fs{instance.event_loop};
	TestBufferedSocketHandler handler{fs};
	fs.Init(s.Release(), FD_SOCKET, std::chrono::seconds{30}, {}, handler);
	fs.ScheduleRead();

	handler.Write("foo"sv);
	EXPECT_EQ(handler.WaitRead(), "foo"sv);
}

TEST(FilteredSocket, NopFilter)
{
	Instance instance;

	auto [s, echo] = NewEchoSocket(instance.event_loop,
				       instance.NewThreadSocketFilter(std::make_unique<NopThreadSocketFilter>()));

	FilteredSocket fs{instance.event_loop};
	TestBufferedSocketHandler handler{fs};
	fs.Init(s.Release(), FD_SOCKET, std::chrono::seconds{30},
		instance.NewThreadSocketFilter(std::make_unique<NopThreadSocketFilter>()),
		handler);
	fs.ScheduleRead();

	handler.Write("foo"sv);
	EXPECT_EQ(handler.WaitRead(), "foo"sv);

	handler.Write("bar"sv);
	EXPECT_EQ(handler.WaitRead(), "bar"sv);
}

TEST(FilteredSocket, Approve)
{
	Instance instance;

	auto h = std::make_unique<ApproveThreadSocketFilter>();
	auto *a = h.get();

	auto [s, echo] = NewEchoSocket(instance.event_loop,
				       instance.NewThreadSocketFilter(std::make_unique<NopThreadSocketFilter>()));

	FilteredSocket fs{instance.event_loop};
	TestBufferedSocketHandler handler{fs};
	fs.Init(s.Release(), FD_SOCKET, std::chrono::seconds{30},
		instance.NewThreadSocketFilter(std::move(h)),
		handler);
	fs.ScheduleRead();

	a->Approve(1);

	handler.Write("foobar"sv);
	EXPECT_EQ(handler.WaitRead(), "f"sv);

	a->Approve(2);
	EXPECT_EQ(handler.WaitRead(), "oo"sv);

	a->Approve(2);
	EXPECT_EQ(handler.WaitRead(), "ba"sv);
}

TEST(FilteredSocket, ApproveClose)
{
	Instance instance;

	auto h = std::make_unique<ApproveThreadSocketFilter>();
	auto *a = h.get();

	auto [s, echo] = NewEchoSocket(instance.event_loop,
				       instance.NewThreadSocketFilter(std::make_unique<NopThreadSocketFilter>()));

	FilteredSocket fs{instance.event_loop};
	TestBufferedSocketHandler handler{fs};
	fs.Init(s.Release(), FD_SOCKET, std::chrono::seconds{30},
		instance.NewThreadSocketFilter(std::move(h)),
		handler);
	fs.ScheduleRead();

	handler.Write("foobar"sv);
	a->Approve(3);
	EXPECT_EQ(handler.WaitRead(), "foo"sv);

	echo->Close();

	a->Approve(4);
	EXPECT_EQ(handler.WaitRead(), "bar"sv);
}

TEST(FilteredSocket, ApproveCloseAfterData)
{
	Instance instance;

	auto h = std::make_unique<ApproveThreadSocketFilter>();
	auto *a = h.get();

	auto [s, echo] = NewEchoSocket(instance.event_loop, {});

	FilteredSocket fs{instance.event_loop};
	TestBufferedSocketHandler handler{fs};
	fs.Init(s.Release(), FD_SOCKET, std::chrono::seconds{30},
		instance.NewThreadSocketFilter(std::move(h)),
		handler);
	fs.ScheduleRead();

	echo->CloseAfterData();

	handler.Write("foobar"sv);
	a->Approve(3);
	EXPECT_EQ(handler.WaitRead(), "foo"sv);

	a->Approve(4);
	EXPECT_EQ(handler.WaitRead(), "bar"sv);
}

TEST(FilteredSocket, Lease)
{
	Instance instance;

	auto h = std::make_unique<ApproveThreadSocketFilter>();
	auto *a = h.get();

	auto [s, echo] = NewEchoSocket(instance.event_loop, {});

	FilteredSocket fs{instance.event_loop};
	fs.InitDummy(s.Release(), FD_SOCKET,
		     instance.NewThreadSocketFilter(std::move(h)));

	struct MyLease final : Lease {
		bool released = false, reuse;

		void ReleaseLease(bool _reuse) noexcept override {
			released = true;
			reuse = _reuse;
		}
	} lease;

	struct MyBufferedSocketHandler final
		: TestBufferedSocketHandler<FilteredSocketLease>
	{
		FilteredSocketLease lease;

		MyBufferedSocketHandler(FilteredSocket &fs,
					Lease &_lease) noexcept
			:TestBufferedSocketHandler(fs.GetEventLoop(), lease),
			 lease(fs, _lease,
			       std::chrono::seconds{30},
			       *this) {}
	};

	MyBufferedSocketHandler handler{fs, lease};
	handler.lease.ScheduleRead();

	echo->CloseAfterData();

	handler.Write("foobar"sv);
	a->Approve(1);
	EXPECT_EQ(handler.WaitRead(), "f"sv);

	handler.BlockData(true);
	a->Approve(1000);

	EXPECT_EQ(handler.WaitRemaining(), 5);
	handler.lease.Release(true, true);

	handler.BlockData(false);
	EXPECT_EQ(handler.WaitRead(), "oobar"sv);
}
