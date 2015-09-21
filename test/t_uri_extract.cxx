#include "uri_extract.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <string.h>
#include <stdlib.h>

static constexpr struct UriTests {
    const char *uri;
    const char *host_and_port;
    const char *path;
    const char *query_string;
} uri_tests[] = {
    { "http://foo/bar", "foo", "/bar", nullptr },
    { "https://foo/bar", "foo", "/bar", nullptr },
    { "ajp://foo/bar", "foo", "/bar", nullptr },
    { "http://foo:8080/bar", "foo:8080", "/bar", nullptr },
    { "http://foo", "foo", nullptr, nullptr },
    { "http://foo/bar?a=b", "foo", "/bar?a=b", "a=b" },
    { "whatever-scheme://foo/bar?a=b", "foo", "/bar?a=b", "a=b" },
    { "//foo/bar", "foo", "/bar", nullptr },
    { "//foo", "foo", nullptr, nullptr },
    { "/bar?a=b", nullptr, "/bar?a=b", "a=b" },
    { "bar?a=b", nullptr, "bar?a=b", "a=b" },
};

class UriExtractTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(UriExtractTest);
    CPPUNIT_TEST(TestUriHostAndPort);
    CPPUNIT_TEST(TestUriPath);
    CPPUNIT_TEST(TestUriQueryString);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestUriHostAndPort() {
        for (auto i : uri_tests) {
            auto result = uri_host_and_port(i.uri);
            if (i.host_and_port == nullptr) {
                CPPUNIT_ASSERT_EQUAL(i.host_and_port, result.data);
                CPPUNIT_ASSERT_EQUAL(result.size, size_t(0));
            } else {
                CPPUNIT_ASSERT(result.data != nullptr);
                CPPUNIT_ASSERT_EQUAL(result.size, strlen(i.host_and_port));
                CPPUNIT_ASSERT_EQUAL(memcmp(i.host_and_port, result.data,
                                            result.size), 0);
            }
        }
    }

    void TestUriPath() {
        for (auto i : uri_tests) {
            auto result = uri_path(i.uri);
            if (i.path == nullptr)
                CPPUNIT_ASSERT_EQUAL(i.path, result);
            else
                CPPUNIT_ASSERT(strcmp(i.path, result) == 0);
        }
    }

    void TestUriQueryString() {
        for (auto i : uri_tests) {
            auto result = uri_query_string(i.uri);
            if (i.query_string == nullptr)
                CPPUNIT_ASSERT_EQUAL(i.query_string, result);
            else
                CPPUNIT_ASSERT(strcmp(i.query_string, result) == 0);
        }
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(UriExtractTest);

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    CppUnit::Test *suite =
        CppUnit::TestFactoryRegistry::getRegistry().makeTest();

    CppUnit::TextUi::TestRunner runner;
    runner.addTest(suite);

    runner.setOutputter(new CppUnit::CompilerOutputter(&runner.result(),
                                                       std::cerr));
    bool success = runner.run();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
