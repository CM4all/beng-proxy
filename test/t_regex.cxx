#include "regex.hxx"
#include "pexpand.hxx"
#include "pool.hxx"
#include "util/Error.hxx"

#include <inline/compiler.h>

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

#include <string.h>
#include <stdlib.h>

class RegexTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(RegexTest);
    CPPUNIT_TEST(TestMatch1);
    CPPUNIT_TEST(TestMatch2);
    CPPUNIT_TEST(TestNotAnchored);
    CPPUNIT_TEST(TestAnchored);
    CPPUNIT_TEST(TestExpand);
    CPPUNIT_TEST_SUITE_END();

public:
    void TestMatch1() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        CPPUNIT_ASSERT(r.Compile(".", false, false, IgnoreError()));
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(r.Match("a"));
        CPPUNIT_ASSERT(r.Match("abc"));
    }

    void TestMatch2() {
        UniqueRegex r = UniqueRegex();
        CPPUNIT_ASSERT(!r.IsDefined());
        CPPUNIT_ASSERT(r.Compile("..", false, false, IgnoreError()));
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(!r.Match("a"));
        CPPUNIT_ASSERT(r.Match("abc"));
    }

    void TestNotAnchored() {
        UniqueRegex r = UniqueRegex();
        CPPUNIT_ASSERT(!r.IsDefined());
        CPPUNIT_ASSERT(r.Compile("/foo/", false, false, IgnoreError()));
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(r.Match("/foo/"));
        CPPUNIT_ASSERT(r.Match("/foo/bar"));
        CPPUNIT_ASSERT(r.Match("foo/foo/"));
    }

    void TestAnchored() {
        UniqueRegex r = UniqueRegex();
        CPPUNIT_ASSERT(!r.IsDefined());
        CPPUNIT_ASSERT(r.Compile("/foo/", true, false, IgnoreError()));
        CPPUNIT_ASSERT(r.IsDefined());
        CPPUNIT_ASSERT(r.Match("/foo/"));
        CPPUNIT_ASSERT(r.Match("/foo/bar"));
        CPPUNIT_ASSERT(!r.Match("foo/foo/"));
    }

    void TestExpand() {
        UniqueRegex r;
        CPPUNIT_ASSERT(!r.IsDefined());
        CPPUNIT_ASSERT(r.Compile("^/foo/(\\w+)/([^/]+)/(.*)$", false, true,
                                 IgnoreError()));
        CPPUNIT_ASSERT(r.IsDefined());

        CPPUNIT_ASSERT(!r.Match("a"));

        auto match_info = r.MatchCapture("/foo/bar/a/b/c.html");
        CPPUNIT_ASSERT(match_info.IsDefined());

        struct pool *pool = pool_new_libc(nullptr, "root");
        auto e = expand_string(pool, "\\1-\\2-\\3-\\\\", match_info,
                               IgnoreError());
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "bar-a-b/c.html-\\") == 0);

        match_info = r.MatchCapture("/foo/bar/a/b/");
        CPPUNIT_ASSERT(match_info.IsDefined());

        e = expand_string(pool, "\\1-\\2-\\3-\\\\", match_info, IgnoreError());
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "bar-a-b/-\\") == 0);

        match_info = r.MatchCapture("/foo/bar/a%20b/c%2520.html");
        CPPUNIT_ASSERT(match_info.IsDefined());

        e = expand_string_unescaped(pool, "\\1-\\2-\\3", match_info,
                                    IgnoreError());
        CPPUNIT_ASSERT(e != nullptr);
        CPPUNIT_ASSERT(strcmp(e, "bar-a b-c%20.html") == 0);

        e = expand_string_unescaped(pool, "\\4", match_info, IgnoreError());
        CPPUNIT_ASSERT(e == nullptr);

        pool_unref(pool);
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(RegexTest);

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

    pool_recycler_clear();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
