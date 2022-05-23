/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "TestPool.hxx"
#include "cluster/BalancerMap.hxx"
#include "cluster/AddressList.hxx"
#include "cluster/AddressListWrapper.hxx"
#include "cluster/AddressListBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "event/Loop.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "net/FailureManager.hxx"
#include "net/FailureRef.hxx"
#include "util/Expiry.hxx"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

class MyBalancer {
	BalancerMap balancer;

	FailureManager &failure_manager;

public:
	explicit MyBalancer(FailureManager &_failure_manager) noexcept
		:failure_manager(_failure_manager) {}

	operator BalancerMap *() {
		return &balancer;
	}

	SocketAddress Get(const AddressList &al, unsigned session=0) {
		return balancer.MakeAddressListWrapper(AddressListWrapper(failure_manager,
									  al),
						       al.sticky_mode)
			.Pick(Expiry::Now(), session);
	}
};

static int
Find(const AddressList &al, SocketAddress address) noexcept
{
	for (unsigned i = 0; i < al.size(); ++i)
		if (al[i] == address)
			return i;

	return -1;
}

[[gnu::pure]]
static FailureStatus
FailureGet(FailureManager &fm, const char *host_and_port)
{
	return fm.Get(Expiry::Now(),
		      ParseSocketAddress(host_and_port, 80, false));
}

static void
FailureAdd(FailureManager &fm, const char *host_and_port,
	   FailureStatus status=FailureStatus::CONNECT,
	   std::chrono::seconds duration=std::chrono::hours(1))
{
	fm.Make(ParseSocketAddress(host_and_port, 80, false))
		.Set(Expiry::Now(), status, duration);
}

static void
FailureRemove(FailureManager &fm, const char *host_and_port,
	      FailureStatus status=FailureStatus::CONNECT)
{
	fm.Make(ParseSocketAddress(host_and_port, 80, false)).Unset(status);
}

TEST(BalancerTest, Failure)
{
	FailureManager fm;

	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::OK);
	ASSERT_EQ(FailureGet(fm, "192.168.0.2"), FailureStatus::OK);

	FailureAdd(fm, "192.168.0.1");
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::CONNECT);
	ASSERT_EQ(FailureGet(fm, "192.168.0.2"), FailureStatus::OK);

	FailureRemove(fm, "192.168.0.1");
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::OK);
	ASSERT_EQ(FailureGet(fm, "192.168.0.2"), FailureStatus::OK);

	/* remove status mismatch */

	FailureAdd(fm, "192.168.0.1", FailureStatus::PROTOCOL);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::OK);
	for (unsigned i = 0; i < 64; ++i)
		FailureAdd(fm, "192.168.0.1", FailureStatus::PROTOCOL);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::PROTOCOL);
	FailureRemove(fm, "192.168.0.1", FailureStatus::CONNECT);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::PROTOCOL);
	FailureRemove(fm, "192.168.0.1", FailureStatus::PROTOCOL);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::OK);

	/* "fade", then "failed", remove "failed", and the old "fade"
	   should remain */

	FailureAdd(fm, "192.168.0.1", FailureStatus::FADE);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::FADE);
	FailureRemove(fm, "192.168.0.1");
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::FADE);
	FailureAdd(fm, "192.168.0.1");
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::CONNECT);
	FailureRemove(fm, "192.168.0.1");
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::FADE);
	FailureRemove(fm, "192.168.0.1", FailureStatus::OK);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::OK);

	/* first "fail", then "fade"; see if removing the "fade"
	   before" failed" will not bring it back */

	FailureAdd(fm, "192.168.0.1", FailureStatus::CONNECT);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::CONNECT);
	FailureAdd(fm, "192.168.0.1", FailureStatus::FADE);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::CONNECT);
	FailureRemove(fm, "192.168.0.1", FailureStatus::CONNECT);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::FADE);
	FailureAdd(fm, "192.168.0.1", FailureStatus::CONNECT);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::CONNECT);
	FailureRemove(fm, "192.168.0.1", FailureStatus::FADE);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::CONNECT);
	FailureRemove(fm, "192.168.0.1", FailureStatus::CONNECT);
	ASSERT_EQ(FailureGet(fm, "192.168.0.1"), FailureStatus::OK);
}

TEST(BalancerTest, Basic)
{
	FailureManager fm;
	TestPool pool;

	EventLoop event_loop;
	MyBalancer balancer(fm);

	const AllocatorPtr alloc{pool};

	AddressListBuilder b;
	b.Add(alloc, ParseSocketAddress("192.168.0.1", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.2", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.3", 80, false));
	const auto al = b.Finish(alloc);

	SocketAddress result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	/* test with session id, which should be ignored here */

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);
}

TEST(BalancerTest, Failed)
{
	FailureManager fm;
	EventLoop event_loop;
	MyBalancer balancer(fm);

	TestPool pool;
	const AllocatorPtr alloc{pool};

	AddressListBuilder b;
	b.Add(alloc, ParseSocketAddress("192.168.0.1", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.2", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.3", 80, false));
	const auto al = b.Finish(alloc);

	FailureAdd(fm, "192.168.0.2");

	SocketAddress result = balancer.Get(al);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_EQ(Find(al, result), 0);
}

TEST(BalancerTest, StickyFailover)
{
	FailureManager fm;
	EventLoop event_loop;
	MyBalancer balancer(fm);

	TestPool pool;
	const AllocatorPtr alloc{pool};

	AddressListBuilder b;
	b.SetStickyMode(StickyMode::FAILOVER);
	b.Add(alloc, ParseSocketAddress("192.168.0.1", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.2", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.3", 80, false));
	const auto al = b.Finish(alloc);

	/* first node is always used */

	SocketAddress result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	/* .. even if the second node fails */

	FailureAdd(fm, "192.168.0.2");

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	/* use third node when both first and second fail */

	FailureAdd(fm, "192.168.0.1");

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	/* use second node when first node fails */

	FailureRemove(fm, "192.168.0.2");

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	/* back to first node as soon as it recovers */

	FailureRemove(fm, "192.168.0.1");

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);
}

TEST(BalancerTest, StickyCookie)
{
	FailureManager fm;
	EventLoop event_loop;
	MyBalancer balancer(fm);

	TestPool pool;
	const AllocatorPtr alloc{pool};

	AddressListBuilder b;
	b.SetStickyMode(StickyMode::COOKIE);
	b.Add(alloc, ParseSocketAddress("192.168.0.1", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.2", 80, false));
	b.Add(alloc, ParseSocketAddress("192.168.0.3", 80, false));
	const auto al = b.Finish(alloc);

	/* without cookie: round-robin */

	SocketAddress result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	/* with cookie */

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al, 1);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al, 2);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al, 2);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al, 3);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 3);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 4);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	result = balancer.Get(al, 4);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 1);

	/* failed */

	FailureAdd(fm, "192.168.0.2");

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	/* fade */

	FailureAdd(fm, "192.168.0.1", FailureStatus::FADE);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al, 3);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al, 3);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 0);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);

	result = balancer.Get(al);
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(Find(al, result), 2);
}
