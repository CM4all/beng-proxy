/*
 * Copyright 2007-2017 Content Management AG
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
#include "balancer.hxx"
#include "AllocatorPtr.hxx"
#include "address_list.hxx"
#include "event/Loop.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/FailureManager.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

class MyBalancer {
    Balancer balancer;

public:
    operator Balancer *() {
        return &balancer;
    }

    SocketAddress Get(const AddressList &al, unsigned session=0) {
        return balancer.Get(al, session);
    }
};

class AddressListBuilder : public AddressList {
    struct pool *pool;

public:
    AddressListBuilder(struct pool *_pool,
                       StickyMode _sticky=StickyMode::NONE)
        :pool(_pool) {
        sticky_mode = _sticky;
    }

    bool Add(const char *host_and_port) {
        return AddressList::Add(*pool,
                                Resolve(host_and_port, 80, nullptr).front());
    }

    int Find(SocketAddress address) const {
        for (unsigned i = 0; i < GetSize(); ++i)
            if (addresses[i] == address)
                return i;

        return -1;
    }
};

gcc_pure
static enum failure_status
FailureGet(const char *host_and_port)
{
    return failure_get_status(Resolve(host_and_port, 80, nullptr).front());
}

static void
FailureAdd(const char *host_and_port,
           enum failure_status status=FAILURE_FAILED,
           std::chrono::seconds duration=std::chrono::hours(1))
{
    failure_set(Resolve(host_and_port, 80, nullptr).front(),
                status, duration);
}

static void
FailureRemove(const char *host_and_port,
              enum failure_status status=FAILURE_FAILED)
{
    failure_unset(Resolve(host_and_port, 80, nullptr).front(),
                status);
}

TEST(BalancerTest, Failure)
{
    const ScopeFailureInit failure;

    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_OK);
    ASSERT_EQ(FailureGet("192.168.0.2"), FAILURE_OK);

    FailureAdd("192.168.0.1");
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FAILED);
    ASSERT_EQ(FailureGet("192.168.0.2"), FAILURE_OK);

    FailureRemove("192.168.0.1");
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_OK);
    ASSERT_EQ(FailureGet("192.168.0.2"), FAILURE_OK);

    /* remove status mismatch */

    FailureAdd("192.168.0.1", FAILURE_RESPONSE);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_RESPONSE);
    FailureRemove("192.168.0.1", FAILURE_FAILED);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_RESPONSE);
    FailureRemove("192.168.0.1", FAILURE_RESPONSE);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_OK);

    /* "fade", then "failed", remove "failed", and the old "fade"
       should remain */

    FailureAdd("192.168.0.1", FAILURE_FADE);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FADE);
    FailureRemove("192.168.0.1");
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FADE);
    FailureAdd("192.168.0.1");
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FAILED);
    FailureRemove("192.168.0.1");
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FADE);
    FailureRemove("192.168.0.1", FAILURE_OK);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_OK);

    /* first "fail", then "fade"; see if removing the "fade"
       before" failed" will not bring it back */

    FailureAdd("192.168.0.1", FAILURE_FAILED);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FAILED);
    FailureAdd("192.168.0.1", FAILURE_FADE);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FAILED);
    FailureRemove("192.168.0.1", FAILURE_FAILED);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FADE);
    FailureAdd("192.168.0.1", FAILURE_FAILED);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FAILED);
    FailureRemove("192.168.0.1", FAILURE_FADE);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_FAILED);
    FailureRemove("192.168.0.1", FAILURE_FAILED);
    ASSERT_EQ(FailureGet("192.168.0.1"), FAILURE_OK);
}

TEST(BalancerTest, Basic)
{
    const ScopeFailureInit failure;
    TestPool pool;

    EventLoop event_loop;
    MyBalancer balancer;

    AddressListBuilder al(pool);
    al.Add("192.168.0.1");
    al.Add("192.168.0.2");
    al.Add("192.168.0.3");

    SocketAddress result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    /* test with session id, which should be ignored here */

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);
}

TEST(BalancerTest, Failed)
{
    const ScopeFailureInit failure;
    EventLoop event_loop;
    MyBalancer balancer;

    TestPool pool;
    AddressListBuilder al(pool);
    al.Add("192.168.0.1");
    al.Add("192.168.0.2");
    al.Add("192.168.0.3");

    FailureAdd("192.168.0.2");

    SocketAddress result = balancer.Get(al);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_EQ(al.Find(result), 0);
}

TEST(BalancerTest, StickyFailover)
{
    const ScopeFailureInit failure;
    EventLoop event_loop;
    MyBalancer balancer;

    TestPool pool;
    AddressListBuilder al(pool, StickyMode::FAILOVER);
    al.Add("192.168.0.1");
    al.Add("192.168.0.2");
    al.Add("192.168.0.3");

    /* first node is always used */

    SocketAddress result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    /* .. even if the second node fails */

    FailureAdd("192.168.0.2");

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    /* use third node when both first and second fail */

    FailureAdd("192.168.0.1");

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    /* use second node when first node fails */

    FailureRemove("192.168.0.2");

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    /* back to first node as soon as it recovers */

    FailureRemove("192.168.0.1");

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);
}

TEST(BalancerTest, StickyCookie)
{
    const ScopeFailureInit failure;
    EventLoop event_loop;
    MyBalancer balancer;

    TestPool pool;
    AddressListBuilder al(pool, StickyMode::COOKIE);
    al.Add("192.168.0.1");
    al.Add("192.168.0.2");
    al.Add("192.168.0.3");

    /* without cookie: round-robin */

    SocketAddress result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    /* with cookie */

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al, 1);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al, 2);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al, 2);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al, 3);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 3);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 4);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    result = balancer.Get(al, 4);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 1);

    /* failed */

    FailureAdd("192.168.0.2");

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    /* fade */

    FailureAdd("192.168.0.1", FAILURE_FADE);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al, 3);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al, 3);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 0);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);

    result = balancer.Get(al);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(al.Find(result), 2);
}
