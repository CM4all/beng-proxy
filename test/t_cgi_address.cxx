#include "PoolTest.hxx"
#include "cgi_address.hxx"
#include "pool.h"

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

class CGIAddressTest : public PoolTest {
    CPPUNIT_TEST_SUITE(CGIAddressTest);
    CPPUNIT_TEST(TestURI);
    CPPUNIT_TEST(TestApply);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestURI() {
        auto pool = GetPool();

        struct cgi_address *a = cgi_address_new(pool, "/usr/bin/cgi", false);
        CPPUNIT_ASSERT_EQUAL(false, a->IsExpandable());

        a->script_name = "/";
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/"));

        a->path_info = "foo";
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/foo"));

        a->query_string = "";
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/foo?"));

        a->query_string = "a=b";
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/foo?a=b"));

        a->path_info = "";
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/?a=b"));

        a->path_info = nullptr;
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/?a=b"));

        a->script_name = "/test.cgi";
        a->path_info = nullptr;
        a->query_string = nullptr;
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/test.cgi"));

        a->path_info = "/foo";
        CPPUNIT_ASSERT_EQUAL(0, strcmp(a->GetURI(pool), "/test.cgi/foo"));
    }

    void TestApply() {
        auto pool = GetPool();

        struct cgi_address *a = cgi_address_new(pool, "/usr/bin/cgi", false);
        a->script_name = "/test.pl";
        a->path_info = "/foo";

        auto b = a->Apply(pool, "", 0, false);
        CPPUNIT_ASSERT_EQUAL((const struct cgi_address *)a, b);

        b = a->Apply(pool, "bar", 3, false);
        CPPUNIT_ASSERT(b != nullptr);
        CPPUNIT_ASSERT(b != a);
        CPPUNIT_ASSERT_EQUAL(false, b->IsValidBase());
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->path, a->path));
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->script_name, a->script_name));
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->path_info, "/bar"));

        a->path_info = "/foo/";
        CPPUNIT_ASSERT_EQUAL(true, a->IsValidBase());

        b = a->Apply(pool, "bar", 3, false);
        CPPUNIT_ASSERT(b != nullptr);
        CPPUNIT_ASSERT(b != a);
        CPPUNIT_ASSERT_EQUAL(false, b->IsValidBase());
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->path, a->path));
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->script_name, a->script_name));
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->path_info, "/foo/bar"));

        b = a->Apply(pool, "/bar", 4, false);
        CPPUNIT_ASSERT(b != nullptr);
        CPPUNIT_ASSERT(b != a);
        CPPUNIT_ASSERT_EQUAL(false, b->IsValidBase());
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->path, a->path));
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->script_name, a->script_name));
        CPPUNIT_ASSERT_EQUAL(0, strcmp(b->path_info, "/bar"));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(CGIAddressTest);

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    CppUnit::Test *suite = CppUnit::TestFactoryRegistry::getRegistry().makeTest();

    CppUnit::TextUi::TestRunner runner;
    runner.addTest(suite);

    runner.setOutputter(new CppUnit::CompilerOutputter(&runner.result(),
                                                       std::cerr));
    bool success =  runner.run();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
