#include "PoolTest.hxx"
#include "failure.hxx"
#include "balancer.hxx"
#include "pool.hxx"
#include "address_list.hxx"
#include "event/Loop.hxx"

#include <inline/compiler.h>
#include <socket/resolver.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <string.h>
#include <stdlib.h>
#include <netdb.h>

class FailureTest : public PoolTest {
public:
    virtual void setUp() {
        PoolTest::setUp();
        failure_init();
    }

    virtual void tearDown() {
        failure_deinit();
        PoolTest::tearDown();
    }
};

class Balancer {
    struct balancer *balancer;

public:
    Balancer(struct pool &pool, EventLoop &event_loop)
        :balancer(balancer_new(pool, event_loop)) {}

    ~Balancer() {
        balancer_free(balancer);
    }

    operator struct balancer *() {
        return balancer;
    }

    SocketAddress Get(const AddressList &al, unsigned session=0) {
        return balancer_get(*balancer, al, session);
    }
};

class AddressListBuilder : public AddressList {
    struct pool *pool;

public:
    AddressListBuilder(struct pool *_pool,
                       StickyMode _sticky=StickyMode::NONE)
        :pool(_pool) {
        AddressList::Init();
        sticky_mode = _sticky;
    }

    bool Add(const char *host_and_port) {
        struct addrinfo *ai;
        int result = socket_resolve_host_port(host_and_port, 80, NULL, &ai);
        if (result != 0)
            return false;

        bool success = AddressList::Add(pool, {ai->ai_addr, ai->ai_addrlen});
        freeaddrinfo(ai);
        return success;
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
    struct addrinfo *ai;
    int result = socket_resolve_host_port(host_and_port, 80, NULL, &ai);
    if (result != 0)
        return FAILURE_FAILED;

    enum failure_status status = failure_get_status({ai->ai_addr, ai->ai_addrlen});
    freeaddrinfo(ai);
    return status;
}

static bool
FailureAdd(const char *host_and_port,
           enum failure_status status=FAILURE_FAILED, unsigned duration=3600)
{
    struct addrinfo *ai;
    int result = socket_resolve_host_port(host_and_port, 80, NULL, &ai);
    if (result != 0)
        return false;

    failure_set({ai->ai_addr, ai->ai_addrlen}, status, duration);
    freeaddrinfo(ai);
    return true;
}

static bool
FailureRemove(const char *host_and_port,
              enum failure_status status=FAILURE_FAILED)
{
    struct addrinfo *ai;
    int result = socket_resolve_host_port(host_and_port, 80, NULL, &ai);
    if (result != 0)
        return false;

    failure_unset({ai->ai_addr, ai->ai_addrlen}, status);
    freeaddrinfo(ai);
    return true;
}

class BalancerTest : public FailureTest {
    CPPUNIT_TEST_SUITE(BalancerTest);
    CPPUNIT_TEST(TestFailure);
    CPPUNIT_TEST(TestBasic);
    CPPUNIT_TEST(TestFailed);
    CPPUNIT_TEST(TestStickyFailover);
    CPPUNIT_TEST(TestStickyCookie);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestFailure() {
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_OK);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.2"), FAILURE_OK);

        FailureAdd("192.168.0.1");
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FAILED);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.2"), FAILURE_OK);

        FailureRemove("192.168.0.1");
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_OK);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.2"), FAILURE_OK);

        /* remove status mismatch */

        FailureAdd("192.168.0.1", FAILURE_RESPONSE);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_RESPONSE);
        FailureRemove("192.168.0.1", FAILURE_FAILED);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_RESPONSE);
        FailureRemove("192.168.0.1", FAILURE_RESPONSE);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_OK);

        /* "fade", then "failed", remove "failed", and the old "fade"
           should remain */

        FailureAdd("192.168.0.1", FAILURE_FADE);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FADE);
        FailureRemove("192.168.0.1");
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FADE);
        FailureAdd("192.168.0.1");
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FAILED);
        FailureRemove("192.168.0.1");
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FADE);
        FailureRemove("192.168.0.1", FAILURE_OK);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_OK);

        /* first "fail", then "fade"; see if removing the "fade"
           before" failed" will not bring it back */

        FailureAdd("192.168.0.1", FAILURE_FAILED);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FAILED);
        FailureAdd("192.168.0.1", FAILURE_FADE);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FAILED);
        FailureRemove("192.168.0.1", FAILURE_FAILED);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FADE);
        FailureAdd("192.168.0.1", FAILURE_FAILED);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FAILED);
        FailureRemove("192.168.0.1", FAILURE_FADE);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_FAILED);
        FailureRemove("192.168.0.1", FAILURE_FAILED);
        CPPUNIT_ASSERT_EQUAL(FailureGet("192.168.0.1"), FAILURE_OK);
    }

    void TestBasic() {
        EventLoop event_loop;
        Balancer balancer(*GetPool(), event_loop);

        AddressListBuilder al(GetPool());
        al.Add("192.168.0.1");
        al.Add("192.168.0.2");
        al.Add("192.168.0.3");

        SocketAddress result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        /* test with session id, which should be ignored here */

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);
    }

    void TestFailed() {
        EventLoop event_loop;
        Balancer balancer(*GetPool(), event_loop);

        AddressListBuilder al(GetPool());
        al.Add("192.168.0.1");
        al.Add("192.168.0.2");
        al.Add("192.168.0.3");

        FailureAdd("192.168.0.2");

        SocketAddress result = balancer.Get(al);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);
    }

    void TestStickyFailover() {
        EventLoop event_loop;
        Balancer balancer(*GetPool(), event_loop);

        AddressListBuilder al(GetPool(), StickyMode::FAILOVER);
        al.Add("192.168.0.1");
        al.Add("192.168.0.2");
        al.Add("192.168.0.3");

        /* first node is always used */

        SocketAddress result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        /* .. even if the second node fails */

        FailureAdd("192.168.0.2");

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        /* use third node when both first and second fail */

        FailureAdd("192.168.0.1");

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        /* use second node when first node fails */

        FailureRemove("192.168.0.2");

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        /* back to first node as soon as it recovers */

        FailureRemove("192.168.0.1");

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);
    }

    void TestStickyCookie() {
        EventLoop event_loop;
        Balancer balancer(*GetPool(), event_loop);

        AddressListBuilder al(GetPool(), StickyMode::COOKIE);
        al.Add("192.168.0.1");
        al.Add("192.168.0.2");
        al.Add("192.168.0.3");

        /* without cookie: round-robin */

        SocketAddress result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        /* with cookie */

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al, 1);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al, 2);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al, 2);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al, 3);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 3);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 4);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        result = balancer.Get(al, 4);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 1);

        /* failed */

        FailureAdd("192.168.0.2");

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        /* fade */

        FailureAdd("192.168.0.1", FAILURE_FADE);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al, 3);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al, 3);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 0);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);

        result = balancer.Get(al);
        CPPUNIT_ASSERT(result != NULL);
        CPPUNIT_ASSERT_EQUAL(al.Find(result), 2);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BalancerTest);

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    EventLoop event_loop;

    CppUnit::Test *suite = CppUnit::TestFactoryRegistry::getRegistry().makeTest();

    CppUnit::TextUi::TestRunner runner;
    runner.addTest(suite);

    runner.setOutputter(new CppUnit::CompilerOutputter(&runner.result(),
                                                       std::cerr));
    bool success =  runner.run();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
