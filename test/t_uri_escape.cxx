#include "uri_escape.hxx"

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <glib.h>

#include <string.h>
#include <stdlib.h>

static constexpr struct UriEscapeData {
    const char *escaped, *unescaped;
} uri_escape_data[] = {
    { "", "" },
    { "%20", " " },
    { "%ff", "\xff" },
    { "%00", nullptr },
    { "%", nullptr },
    { "%1", nullptr },
    { "%gg", nullptr },
    { "foo", "foo" },
    { "foo%20bar", "foo bar" },
    { "foo%25bar", "foo%bar" },
    { "foo%2525bar", "foo%25bar" },
};

class UriEscapeTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(UriEscapeTest);
    CPPUNIT_TEST(TestUriEscape);
    CPPUNIT_TEST(TestUriUnescapeInplace);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestUriEscape() {
        for (auto i : uri_escape_data) {
            if (i.unescaped == nullptr)
                continue;

            char buffer[256];
            size_t length = uri_escape(buffer, i.unescaped,
                                       strlen(i.unescaped));
            CPPUNIT_ASSERT_EQUAL(length, strlen(i.escaped));
            CPPUNIT_ASSERT(memcmp(buffer, i.escaped, length) == 0);
        }
    }

    void TestUriUnescape() {
        for (auto i : uri_escape_data) {
            char buffer[256];
            strcpy(buffer, i.escaped);

            auto result = uri_unescape(buffer, i.escaped, strlen(i.escaped));
            if (i.unescaped == nullptr) {
                CPPUNIT_ASSERT_EQUAL(result, (char *)nullptr);
            } else {
                size_t length = result - buffer;
                CPPUNIT_ASSERT_EQUAL(length, strlen(i.unescaped));
                CPPUNIT_ASSERT(memcmp(buffer, i.unescaped, length) == 0);
            }
        }
    }

    void TestUriUnescapeInplace() {
        for (auto i : uri_escape_data) {
            char buffer[256];
            strcpy(buffer, i.escaped);

            size_t length = uri_unescape_inplace(buffer, strlen(buffer));
            if (i.unescaped == nullptr) {
                CPPUNIT_ASSERT_EQUAL(length, size_t(0));
            } else {
                CPPUNIT_ASSERT_EQUAL(length, strlen(i.unescaped));
                CPPUNIT_ASSERT(memcmp(buffer, i.unescaped, length) == 0);
            }
        }
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(UriEscapeTest);

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
